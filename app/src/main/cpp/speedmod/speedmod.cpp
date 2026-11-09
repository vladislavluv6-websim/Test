#include <jni.h>

#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

// Speed module: port of the SourceBlix (PC) Speed feature.
//
// Modes:
//   0 Breadix  - absolute horizontal speed on ground and for 3 air ticks
//                after a jump.
//   1 TheHive  - additive speed on ground and for `tickSpeed` air ticks
//                after a jump; optional "avoid check" drops the position in
//                PlayerAuthInput by one block to dodge the strafe check.
//   2 Strafe   - absolute horizontal speed along the current move direction.
//   3 Pheonix  - absolute speed plus forced START_GLIDING/START_SWIMMING
//                input bits and cleared sneaking/stop bits.
//
// The velocity is written directly into StateVectorComponent before the
// vanilla actor tick runs, exactly like the upstream BaseTick handler.
namespace {

constexpr const char *kTag = "SpeedMod";
constexpr const char *kMinecraftLibrary = "libminecraftpe.so";

constexpr const char *kNormalTickSignature =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "? ? ? D1 54 D0 3B D5 F3 03 00 AA ? ? ? F9 ? ? ? F8 ? ? ? 39";

constexpr const char *kClientInstanceUpdateSignature =
    "? ? ? FC ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "? ? ? D1 59 D0 3B D5 ? ? ? 52 F5 03 02 2A";

constexpr const char *kClientInstanceGetLocalPlayerSignature =
    "? ? ? D1 ? ? ? A9 ? ? ? F9 ? ? ? 91 53 D0 3B D5 E8 03 00 AA ? ? ? 91 ? ? ? F9 "
    "? ? ? 91 ? ? ? F8 ? ? ? ? ? ? ? 91 ? ? ? ? ? ? ? 36 ? ? ? 91 ? ? ? 52 ? ? ? ? "
    "? ? ? ? E0 03 1F AA ? ? ? F9 ? ? ? F8 1F 01 09 EB ? ? ? 54 ? ? ? A9 ? ? ? F9 "
    "? ? ? 91 C0 03 5F D6 ? ? ? ? C0 03 5F D6";

// Actor -> StateVectorComponent* (position, previous position, velocity).
constexpr uintptr_t kActorStateVectorComponent = 0x228;
constexpr uintptr_t kStatePosition = 0;
constexpr uintptr_t kStatePreviousPosition = 12;
constexpr uintptr_t kStateVelocity = 24;

// 1.26.60.04: PlayerAuthInputPacket::write(BinaryStream&, CerealContext&).
constexpr uintptr_t kPlayerAuthInputWriteOffset = 0xdaa4864;

// PlayerAuthInputPacket layout in 1.26.60.04:
//   +0x30 mRot (Vec2 pitch/yaw)
//   +0x38 mPos (Vec3)
//   +0x48 mPosDelta (Vec3)
//   +0x88 InputData bitset (uint64 array)
constexpr uintptr_t kPacketRot = 0x30;
constexpr uintptr_t kPacketPos = 0x38;
constexpr uintptr_t kPacketInputData = 0x88;

// PlayerAuthInputData bits (Bedrock protocol).
constexpr int kInputSneaking = 8;
constexpr int kInputSneakToggleDown = 23;
constexpr int kInputStartSneaking = 27;
constexpr int kInputStartSwimming = 29;
constexpr int kInputStopSwimming = 30;
constexpr int kInputStartJumping = 31;
constexpr int kInputStartGliding = 32;
constexpr int kInputStopGliding = 33;

constexpr int kModeBreadix = 0;
constexpr int kModeTheHive = 1;
constexpr int kModeStrafe = 2;
constexpr int kModePheonix = 3;

constexpr int kJumpVanilla = 0;
constexpr int kJumpVelocity = 1;
constexpr int kJumpNone = 2;

using NormalTickFn = void (*)(void *);
using BaseTickFn = void (*)(void *);
using ClientInstanceUpdateFn = void *(*)(void *, bool);
using GetLocalPlayerFn = void *(*)(void *);
using PlayerAuthInputWriteFn = void *(*)(void *, void *, void *, void *);

NormalTickFn g_normalTickOriginal = nullptr;
BaseTickFn g_baseTickOriginal = nullptr;
ClientInstanceUpdateFn g_clientInstanceUpdateOriginal = nullptr;
GetLocalPlayerFn g_getLocalPlayer = nullptr;
PlayerAuthInputWriteFn g_playerAuthWriteOriginal = nullptr;

std::atomic_bool g_enabled{false};
std::atomic_int g_mode{kModeBreadix};
std::atomic_int g_speedRaw{50};        // speed * 100 (0.50)
std::atomic_int g_tickSpeed{3};        // TheHive air ticks
std::atomic_int g_jumpType{kJumpVanilla};
std::atomic_int g_jumpHeightRaw{42};   // jump height * 100 (0.42)
std::atomic_bool g_avoidCheck{false};
std::atomic_int g_avoidDelayMs{400};
std::atomic_bool g_debug{false};
std::atomic<float> g_legacyMultiplier{1.0f};

std::atomic<void *> g_clientInstance{nullptr};
std::atomic<void *> g_clientLocalPlayer{nullptr};
std::atomic<void *> g_localPlayer{nullptr};
std::atomic<float> g_lastYaw{0.0f};
std::atomic_bool g_pendingJump{false};
std::atomic_bool g_avoidClipPending{false};
std::atomic_bool g_onGround{true};
std::atomic_bool g_moving{false};

std::mutex g_initMutex;
bool g_clientHookInstalled = false;
bool g_tickHookInstalled = false;
bool g_baseTickHookInstalled = false;
bool g_baseTickHookAttempted = false;
bool g_packetHookInstalled = false;
bool g_installed = false;
uintptr_t g_normalTickAddress = 0;
uintptr_t g_baseTickAddress = 0;
std::chrono::steady_clock::time_point g_lastInitAttempt{};

// Tick-thread only state.
void *g_tickRuntime = nullptr;
int g_airTicks = 0;
bool g_jumpSampled = false;
bool g_jumpWasGround = false;
int g_jumpWindowTicks = 11;
int64_t g_lastAvoidAt = 0;
uint32_t g_tickCounter = 0;

void logInfo(const char *message) {
  __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message);
}

void logWarning(const char *message) {
  __android_log_print(ANDROID_LOG_WARN, kTag, "%s", message);
}

float horizontalLength(float x, float z) { return std::sqrt(x * x + z * z); }

int64_t monotonicMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void resetMovementState() {
  g_pendingJump.store(false, std::memory_order_relaxed);
  g_avoidClipPending.store(false, std::memory_order_relaxed);
  g_tickRuntime = nullptr;
  g_airTicks = 0;
  g_jumpSampled = false;
  g_jumpWasGround = false;
  g_jumpWindowTicks = 11;
  g_lastAvoidAt = 0;
  g_tickCounter = 0;
}

// Mirrors SpeedPolicy::JumpWindow from the upstream client.
void observeJumpWindow(bool ground, float vertical) {
  if (ground) {
    g_jumpWindowTicks = 0;
  } else if (g_jumpSampled && g_jumpWasGround && std::isfinite(vertical) &&
             vertical > 0.0f) {
    g_jumpWindowTicks = 1;
  } else if (g_jumpWindowTicks > 0 && g_jumpWindowTicks < 11) {
    ++g_jumpWindowTicks;
  } else {
    g_jumpWindowTicks = 11;
  }
  g_jumpSampled = true;
  g_jumpWasGround = ground;
  g_airTicks = ground ? 0 : std::min(g_airTicks + 1, 1000000);
}

bool jumpWindowActive(int ticks) {
  const int limit = std::max(0, std::min(ticks, 10));
  return g_jumpWindowTicks >= 1 && g_jumpWindowTicks <= limit;
}

void setInputBit(uint64_t *bits, int bit, bool value) {
  const uint64_t mask = 1ULL << (bit & 63);
  if (value) {
    bits[bit >> 6] |= mask;
  } else {
    bits[bit >> 6] &= ~mask;
  }
}

float speedAmount() {
  float amount = static_cast<float>(g_speedRaw.load(std::memory_order_relaxed)) / 100.0f;
  float multiplier = g_legacyMultiplier.load(std::memory_order_relaxed);
  if (!std::isfinite(multiplier) || multiplier < 1.0f) multiplier = 1.0f;
  if (multiplier > 5.0f) multiplier = 5.0f;
  amount *= multiplier;
  if (!std::isfinite(amount)) return 0.0f;
  return std::max(0.0f, std::min(amount, 10.0f));
}

void applySpeed(void *actor) {
  if (!actor) return;

  const bool master = g_enabled.load(std::memory_order_relaxed);
  if (!master) return;

  void *localPlayer = g_localPlayer.load(std::memory_order_relaxed);
  if (localPlayer && actor != localPlayer) return;

  auto *base = reinterpret_cast<char *>(actor);
  auto stateVector = *reinterpret_cast<uintptr_t *>(
      base + kActorStateVectorComponent);
  if (stateVector < 0x1000 || (stateVector & 0x7) != 0) return;

  auto *position = reinterpret_cast<float *>(stateVector + kStatePosition);
  auto *previous =
      reinterpret_cast<float *>(stateVector + kStatePreviousPosition);
  auto *velocity = reinterpret_cast<float *>(stateVector + kStateVelocity);

  constexpr float kWorldLimit = 3.0e7f;
  if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2]) || !std::isfinite(previous[0]) ||
      !std::isfinite(previous[1]) || !std::isfinite(previous[2]) ||
      std::fabs(position[0]) > kWorldLimit ||
      std::fabs(position[1]) > kWorldLimit ||
      std::fabs(position[2]) > kWorldLimit) {
    return;
  }

  if (horizontalLength(position[0] - previous[0],
                       position[2] - previous[2]) > 10.0f) {
    return;
  }

  float vx = velocity[0];
  float vz = velocity[2];
  float vy = velocity[1];
  if (!std::isfinite(vx) || !std::isfinite(vz) || !std::isfinite(vy)) return;

  if (g_tickRuntime != actor) {
    g_tickRuntime = actor;
    g_airTicks = 0;
    g_jumpSampled = false;
    g_jumpWasGround = false;
    g_jumpWindowTicks = 11;
  }

  const float dx = position[0] - previous[0];
  const float dz = position[2] - previous[2];
  const float dy = position[1] - previous[1];
  const float current = horizontalLength(vx, vz);
  const float moveDist = horizontalLength(dx, dz);
  const bool moving = moveDist > 0.003f || current > 0.005f;
  // The BaseTick hook has no direct OnGround accessor, so infer it from the
  // vertical delta/velocity. Airborne ticks always carry vertical motion.
  const bool ground = std::fabs(dy) < 0.002f && std::fabs(vy) < 0.01f;

  g_onGround.store(ground, std::memory_order_relaxed);
  g_moving.store(moving, std::memory_order_relaxed);
  observeJumpWindow(ground, vy);

  const int mode = g_mode.load(std::memory_order_relaxed);
  const float amount = speedAmount();
  const int tickSpeed = g_tickSpeed.load(std::memory_order_relaxed);

  float dirX = 0.0f;
  float dirZ = 0.0f;
  if (current > 0.01f) {
    dirX = vx / current;
    dirZ = vz / current;
  } else {
    const float yaw = g_lastYaw.load(std::memory_order_relaxed);
    if (std::isfinite(yaw)) {
      constexpr float kDegToRad = 0.01745329251994329577f;
      dirX = -std::sin(yaw * kDegToRad);
      dirZ = std::cos(yaw * kDegToRad);
    }
  }

  if (moving && amount > 0.0f) {
    float target = current;
    bool absolute = false;
    switch (mode) {
      case kModeBreadix:
        if (ground || jumpWindowActive(3)) {
          target = std::max(current, amount);
          absolute = true;
        }
        break;
      case kModeTheHive:
        if (ground || jumpWindowActive(tickSpeed)) {
          // Additive blocks per tick, matching SpeedPolicy::targetSpeed with
          // a 20 tps simulation. The speed setting is clamped to 1 like the
          // upstream policy.
          target = current + std::min(amount, 1.0f) / 20.0f;
          absolute = true;
        }
        break;
      case kModeStrafe:
      case kModePheonix:
        target = amount;
        absolute = true;
        break;
      default:
        break;
    }

    if (absolute && target > current && horizontalLength(dirX, dirZ) > 0.0001f) {
      vx = dirX * target;
      vz = dirZ * target;
      velocity[0] = vx;
      velocity[2] = vz;

      if (g_debug.load(std::memory_order_relaxed) && (++g_tickCounter % 40) == 0) {
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "mode=%d speed=%.2f cur=%.3f -> %.3f ground=%d air=%d win=%d",
            mode, static_cast<double>(amount), static_cast<double>(current),
            static_cast<double>(target), ground ? 1 : 0, g_airTicks,
            g_jumpWindowTicks);
      }
    }
  }

  // Jump handling: only when moving, like the upstream tickMovement.
  const int jumpType = g_jumpType.load(std::memory_order_relaxed);
  const bool jumping = jumpType == kJumpVanilla || jumpType == kJumpVelocity;
  if (moving && ground && jumpType != kJumpNone && jumping) {
    const float height =
        static_cast<float>(g_jumpHeightRaw.load(std::memory_order_relaxed)) / 100.0f;
    if (std::isfinite(height) && height > 0.0f) {
      velocity[1] = std::min(height, 1.0f);
      if (jumpType == kJumpVanilla) {
        g_pendingJump.store(true, std::memory_order_release);
      }
    }
  }

  // Avoid check (Breadix/TheHive only): periodically report a position one
  // block lower so the server does not flag the accelerated strafe.
  const bool avoid = g_avoidCheck.load(std::memory_order_relaxed);
  if (avoid && moving && ground &&
      (mode == kModeBreadix || mode == kModeTheHive)) {
    const int64_t now = monotonicMs();
    const int64_t delay =
        static_cast<int64_t>(g_avoidDelayMs.load(std::memory_order_relaxed));
    if (now - g_lastAvoidAt >= delay) {
      g_lastAvoidAt = now;
      g_avoidClipPending.store(true, std::memory_order_release);
    }
  }
}

void *playerAuthInputWriteDetour(void *packet, void *a1, void *a2, void *a3) {
  if (packet && g_enabled.load(std::memory_order_relaxed)) {
    auto *pos = reinterpret_cast<float *>(
        static_cast<char *>(packet) + kPacketPos);
    auto *inputBits = reinterpret_cast<uint64_t *>(
        static_cast<char *>(packet) + kPacketInputData);

    const float yaw = *reinterpret_cast<float *>(
        static_cast<char *>(packet) + kPacketRot + sizeof(float));
    if (std::isfinite(yaw)) {
      g_lastYaw.store(yaw, std::memory_order_relaxed);
    }

    const int mode = g_mode.load(std::memory_order_relaxed);
    if (mode == kModePheonix) {
      setInputBit(inputBits, kInputStopGliding, false);
      setInputBit(inputBits, kInputStopSwimming, false);
      setInputBit(inputBits, kInputSneaking, false);
      setInputBit(inputBits, kInputSneakToggleDown, false);
      setInputBit(inputBits, kInputStartSneaking, false);
      setInputBit(inputBits, kInputStartGliding, true);
      setInputBit(inputBits, kInputStartSwimming, true);
    }

    if (g_pendingJump.exchange(false, std::memory_order_acq_rel)) {
      setInputBit(inputBits, kInputStartJumping, true);
    }

    if (g_avoidClipPending.exchange(false, std::memory_order_acq_rel) &&
        std::isfinite(pos[1])) {
      pos[1] -= 1.0f;
    }
  }

  return g_playerAuthWriteOriginal
             ? g_playerAuthWriteOriginal(packet, a1, a2, a3)
             : nullptr;
}

bool isMinecraftAddress(uintptr_t address) {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void *>(address), &info) == 0) return true;
  return info.dli_fname == nullptr ||
         std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

void baseTickDetour(void *actor) {
  void *localPlayer = g_localPlayer.load(std::memory_order_relaxed);
  if (localPlayer && actor == localPlayer) applySpeed(actor);
  if (g_baseTickOriginal) g_baseTickOriginal(actor);
}

bool installBaseTickHook(void *localPlayer) {
  if (g_baseTickHookInstalled) return true;
  if (g_baseTickHookAttempted || !localPlayer) return false;
  g_baseTickHookAttempted = true;

  auto **vtable = *reinterpret_cast<void ***>(localPlayer);
  if (!vtable) {
    logWarning("Local player vtable is null");
    return false;
  }

  const uintptr_t baseTick = reinterpret_cast<uintptr_t>(vtable[25]);
  if (!baseTick || baseTick == g_normalTickAddress) {
    logWarning("Actor::baseTick vtable entry is invalid");
    return false;
  }
  if (!isMinecraftAddress(baseTick)) {
    logWarning("Actor::baseTick vtable entry is not in libminecraftpe");
    return false;
  }
  if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(baseTick),
                       reinterpret_cast<pl::memory::FuncPtr>(baseTickDetour),
                       reinterpret_cast<pl::memory::FuncPtr *>(
                           &g_baseTickOriginal)) != 0) {
    logWarning("Unable to hook Actor::baseTick");
    return false;
  }

  g_baseTickAddress = baseTick;
  g_baseTickHookInstalled = true;
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "Actor::baseTick hooked at %p (vtable[25])",
                      reinterpret_cast<void *>(baseTick));
  return true;
}

void normalTickDetour(void *actor) {
  if (actor) {
    void *clientLocal = g_clientLocalPlayer.load(std::memory_order_relaxed);
    if (!clientLocal || actor == clientLocal) {
      void *previous =
          g_localPlayer.exchange(actor, std::memory_order_acq_rel);
      if (!previous) {
        __android_log_print(ANDROID_LOG_INFO, kTag,
                            "local player from normalTick=%p", actor);
      } else if (previous != actor) {
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "local player changed %p -> %p", previous, actor);
      }
    }
    if (!g_clientLocalPlayer.load(std::memory_order_relaxed) &&
        !g_baseTickHookInstalled && !g_baseTickHookAttempted) {
      installBaseTickHook(actor);
    }
  }

  if (!g_baseTickHookInstalled) applySpeed(actor);
  if (g_normalTickOriginal) g_normalTickOriginal(actor);
}

void *clientInstanceUpdateDetour(void *clientInstance, bool value) {
  void *result = g_clientInstanceUpdateOriginal
                     ? g_clientInstanceUpdateOriginal(clientInstance, value)
                     : nullptr;

  if (clientInstance) {
    g_clientInstance.store(clientInstance, std::memory_order_release);
    if (g_getLocalPlayer) {
      void *localPlayer = g_getLocalPlayer(clientInstance);
      g_clientLocalPlayer.store(localPlayer, std::memory_order_release);
      if (localPlayer) {
        void *previous =
            g_localPlayer.exchange(localPlayer, std::memory_order_acq_rel);
        if (localPlayer != previous) {
          __android_log_print(ANDROID_LOG_INFO, kTag, "local player=%p",
                              localPlayer);
          resetMovementState();
        }
        installBaseTickHook(localPlayer);
      }
    }
  }

  return result;
}

bool installPacketHook(uintptr_t gameBase) {
  if (g_packetHookInstalled) return true;
  if (gameBase == 0) return false;

  const uintptr_t address = gameBase + kPlayerAuthInputWriteOffset;
  static const unsigned char kPrologue[16] = {
      0xfd, 0x7b, 0xbc, 0xa9, 0xf7, 0x0b, 0x00, 0xf9,
      0xf6, 0x57, 0x02, 0xa9, 0xf4, 0x4f, 0x03, 0xa9};
  if (std::memcmp(reinterpret_cast<const void *>(address), kPrologue,
                  sizeof(kPrologue)) != 0) {
    __android_log_print(
        ANDROID_LOG_WARN, kTag,
        "PlayerAuthInput::write prologue mismatch at %p (unsupported version)",
        reinterpret_cast<void *>(address));
    return false;
  }

  if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(address),
                       reinterpret_cast<pl::memory::FuncPtr>(
                           playerAuthInputWriteDetour),
                       reinterpret_cast<pl::memory::FuncPtr *>(
                           &g_playerAuthWriteOriginal)) != 0) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "Unable to hook PlayerAuthInput::write at %p",
                        reinterpret_cast<void *>(address));
    return false;
  }

  g_packetHookInstalled = true;
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "PlayerAuthInput::write hooked at %p",
                      reinterpret_cast<void *>(address));
  return true;
}

bool initialize() {
  std::lock_guard<std::mutex> lock(g_initMutex);
  if (g_installed) return true;

  const auto now = std::chrono::steady_clock::now();
  if (g_lastInitAttempt.time_since_epoch().count() != 0 &&
      now - g_lastInitAttempt < std::chrono::seconds(2)) {
    return false;
  }
  g_lastInitAttempt = now;

  const uintptr_t normalTickAddress =
      pl::memory::resolveSignature(kNormalTickSignature, kMinecraftLibrary);
  const uintptr_t clientUpdateAddress = pl::memory::resolveSignature(
      kClientInstanceUpdateSignature, kMinecraftLibrary);
  const uintptr_t localPlayerAddress = pl::memory::resolveSignature(
      kClientInstanceGetLocalPlayerSignature, kMinecraftLibrary);

  if (!normalTickAddress || !clientUpdateAddress || !localPlayerAddress) {
    logWarning("Unable to resolve required Minecraft signatures");
    return false;
  }

  g_normalTickAddress = normalTickAddress;

  __android_log_print(
      ANDROID_LOG_INFO, kTag,
      "1.26.60 signatures: normalTick=%p clientUpdate=%p getLocalPlayer=%p",
      reinterpret_cast<void *>(normalTickAddress),
      reinterpret_cast<void *>(clientUpdateAddress),
      reinterpret_cast<void *>(localPlayerAddress));

  if (!g_getLocalPlayer) {
    g_getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(localPlayerAddress);
  }

  if (!g_clientHookInstalled) {
    if (pl::memory::hook(
            reinterpret_cast<pl::memory::FuncPtr>(clientUpdateAddress),
            reinterpret_cast<pl::memory::FuncPtr>(clientInstanceUpdateDetour),
            reinterpret_cast<pl::memory::FuncPtr *>(
                &g_clientInstanceUpdateOriginal)) != 0) {
      logWarning("Unable to hook ClientInstance::update");
      return false;
    }
    g_clientHookInstalled = true;
  }

  if (!g_tickHookInstalled) {
    if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(
                             normalTickAddress),
                         reinterpret_cast<pl::memory::FuncPtr>(normalTickDetour),
                         reinterpret_cast<pl::memory::FuncPtr *>(
                             &g_normalTickOriginal)) != 0) {
      logWarning("Unable to hook LocalPlayer::normalTick");
      return false;
    }
    g_tickHookInstalled = true;
  }

  Dl_info info{};
  uintptr_t gameBase = 0;
  if (dladdr(reinterpret_cast<void *>(normalTickAddress), &info) != 0) {
    gameBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
  }
  if (!installPacketHook(gameBase)) {
    logWarning("Pheonix/avoid-check packet edits disabled");
  }

  g_installed = true;
  logInfo("Speed module initialized (SourceBlix port)");
  return true;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_SpeedMod_nativeInit(
    JNIEnv *, jclass) {
  return initialize() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_SpeedMod_nativeSetEnabled(
    JNIEnv *, jclass, jboolean enabled) {
  const bool value = enabled == JNI_TRUE;
  g_enabled.store(value, std::memory_order_relaxed);
  if (!value) resetMovementState();
  __android_log_print(ANDROID_LOG_INFO, kTag, "enabled=%d", value ? 1 : 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_SpeedMod_nativeIsEnabled(
    JNIEnv *, jclass) {
  return g_enabled.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_SpeedMod_nativeSetMultiplier(
    JNIEnv *, jclass, jfloat multiplier) {
  if (multiplier < 1.0f) multiplier = 1.0f;
  if (multiplier > 5.0f) multiplier = 5.0f;
  g_legacyMultiplier.store(multiplier, std::memory_order_relaxed);
}

#define SPEED_BOOL_SETTER(name, field)                                        \
  extern "C" JNIEXPORT void JNICALL                                           \
      Java_org_levimc_launcher_core_mods_inbuilt_nativemod_SpeedMod_##name(   \
          JNIEnv *, jclass, jboolean value) {                                 \
    field.store(value == JNI_TRUE, std::memory_order_relaxed);                \
  }

#define SPEED_INT_SETTER(name, field)                                         \
  extern "C" JNIEXPORT void JNICALL                                           \
      Java_org_levimc_launcher_core_mods_inbuilt_nativemod_SpeedMod_##name(   \
          JNIEnv *, jclass, jint value) {                                     \
    field.store(static_cast<int>(value), std::memory_order_relaxed);          \
  }

SPEED_INT_SETTER(nativeSetSpeedMode, g_mode)
SPEED_INT_SETTER(nativeSetSpeedValue, g_speedRaw)
SPEED_INT_SETTER(nativeSetTickSpeed, g_tickSpeed)
SPEED_INT_SETTER(nativeSetJumpType, g_jumpType)
SPEED_INT_SETTER(nativeSetJumpHeight, g_jumpHeightRaw)
SPEED_INT_SETTER(nativeSetAvoidDelay, g_avoidDelayMs)
SPEED_BOOL_SETTER(nativeSetAvoidCheck, g_avoidCheck)
SPEED_BOOL_SETTER(nativeSetDebug, g_debug)
