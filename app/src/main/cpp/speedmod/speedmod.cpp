#include <jni.h>

#include <android/log.h>
#include <dlfcn.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

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

constexpr uintptr_t kActorStateVectorComponent = 0x228;
constexpr uintptr_t kStatePosition = 0;
constexpr uintptr_t kStatePreviousPosition = 12;
constexpr uintptr_t kStateVelocity = 24;
constexpr float kMaxExtraBlocksPerTick = 0.75f;

using NormalTickFn = void (*)(void *);
using BaseTickFn = void (*)(void *);
using ClientInstanceUpdateFn = void *(*)(void *, bool);
using GetLocalPlayerFn = void *(*)(void *);

NormalTickFn g_normalTickOriginal = nullptr;
BaseTickFn g_baseTickOriginal = nullptr;
ClientInstanceUpdateFn g_clientInstanceUpdateOriginal = nullptr;
GetLocalPlayerFn g_getLocalPlayer = nullptr;

std::atomic_bool g_enabled{false};
std::atomic<float> g_multiplier{2.0f};
std::atomic<void *> g_clientLocalPlayer{nullptr};
std::atomic<void *> g_localPlayer{nullptr};
std::atomic<uint32_t> g_tickLogCounter{0};
std::atomic<uint32_t> g_mismatchLogCounter{0};

std::mutex g_initMutex;
bool g_clientHookInstalled = false;
bool g_tickHookInstalled = false;
bool g_baseTickHookInstalled = false;
bool g_baseTickHookAttempted = false;
bool g_installed = false;
uintptr_t g_normalTickAddress = 0;
uintptr_t g_baseTickAddress = 0;
std::chrono::steady_clock::time_point g_lastInitAttempt{};

void logInfo(const char *message) {
  __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message);
}

void logWarning(const char *message) {
  __android_log_print(ANDROID_LOG_WARN, kTag, "%s", message);
}

float horizontalLength(float x, float z) {
  return std::sqrt(x * x + z * z);
}

struct TickState {
  void *actor = nullptr;
  float posX = 0.0f;
  float posZ = 0.0f;
  float baseVx = 0.0f;
  float baseVz = 0.0f;
  float multiplier = 1.0f;
  bool applied = false;
};

thread_local TickState g_tickState;

void applySpeed(void *actor) {
  g_tickState.applied = false;
  if (!actor || !g_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  void *localPlayer = g_localPlayer.load(std::memory_order_relaxed);
  if (localPlayer && actor != localPlayer) {
    uint32_t count = g_mismatchLogCounter.fetch_add(1, std::memory_order_relaxed);
    if (count == 0) {
      __android_log_print(ANDROID_LOG_WARN, kTag,
                          "tick actor %p does not match local player %p",
                          actor, localPlayer);
    }
  }

  auto *base = reinterpret_cast<char *>(actor);
  auto stateVector = *reinterpret_cast<uintptr_t *>(base + kActorStateVectorComponent);
  if (stateVector < 0x1000 || (stateVector & 0x7) != 0) {
    return;
  }

  auto *position = reinterpret_cast<float *>(stateVector + kStatePosition);
  auto *previous = reinterpret_cast<float *>(stateVector + kStatePreviousPosition);
  auto *velocity = reinterpret_cast<float *>(stateVector + kStateVelocity);

  constexpr float kWorldLimit = 3.0e7f;
  if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2]) || !std::isfinite(previous[0]) ||
      !std::isfinite(previous[1]) || !std::isfinite(previous[2]) ||
      std::fabs(position[0]) > kWorldLimit ||
      std::fabs(position[1]) > kWorldLimit || std::fabs(position[2]) > kWorldLimit) {
    return;
  }

  if (horizontalLength(position[0] - previous[0], position[2] - previous[2]) > 10.0f) {
    return;
  }

  float vx = velocity[0];
  float vz = velocity[2];
  if (!std::isfinite(vx) || !std::isfinite(vz)) {
    return;
  }
  if (horizontalLength(vx, vz) < 1.0e-5f) {
    vx = position[0] - previous[0];
    vz = position[2] - previous[2];
  }

  const float movement = horizontalLength(vx, vz);
  if (movement < 1.0e-5f || movement > 2.0f) {
    return;
  }

  float multiplier = g_multiplier.load(std::memory_order_relaxed);
  if (multiplier <= 1.0f) {
    return;
  }

  float extraX = vx * (multiplier - 1.0f);
  float extraZ = vz * (multiplier - 1.0f);
  const float extraLength = horizontalLength(extraX, extraZ);
  if (extraLength > kMaxExtraBlocksPerTick) {
    const float scale = kMaxExtraBlocksPerTick / extraLength;
    extraX *= scale;
    extraZ *= scale;
  }

  g_tickState.actor = actor;
  g_tickState.posX = position[0];
  g_tickState.posZ = position[2];
  g_tickState.baseVx = vx;
  g_tickState.baseVz = vz;
  g_tickState.multiplier = multiplier;
  g_tickState.applied = true;

  velocity[0] = vx + extraX;
  velocity[2] = vz + extraZ;

  uint32_t count = g_tickLogCounter.fetch_add(1, std::memory_order_relaxed);
  if (count % 20 == 0) {
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "speed actor=%p pos=(%.3f,%.3f) vel=(%.3f,%.3f) -> (%.3f,%.3f) mult=%.2f",
        actor, position[0], position[2], vx, vz, velocity[0], velocity[2],
        multiplier);
  }
}

void applyPositionFallback(void *actor) {
  if (!g_tickState.applied || g_tickState.actor != actor) {
    return;
  }

  auto *base = reinterpret_cast<char *>(actor);
  auto stateVector = *reinterpret_cast<uintptr_t *>(base + kActorStateVectorComponent);
  if (stateVector < 0x1000 || (stateVector & 0x7) != 0) {
    return;
  }

  auto *position = reinterpret_cast<float *>(stateVector + kStatePosition);
  if (!std::isfinite(position[0]) || !std::isfinite(position[2])) {
    return;
  }

  const float actual = horizontalLength(position[0] - g_tickState.posX,
                                        position[2] - g_tickState.posZ);
  const float baseMovement =
      horizontalLength(g_tickState.baseVx, g_tickState.baseVz);
  if (baseMovement < 1.0e-5f || actual < 1.0e-5f ||
      actual >= baseMovement * 1.15f) {
    return;
  }

  float extraX = g_tickState.baseVx * (g_tickState.multiplier - 1.0f);
  float extraZ = g_tickState.baseVz * (g_tickState.multiplier - 1.0f);
  const float extraLength = horizontalLength(extraX, extraZ);
  if (extraLength > kMaxExtraBlocksPerTick) {
    const float scale = kMaxExtraBlocksPerTick / extraLength;
    extraX *= scale;
    extraZ *= scale;
  }

  position[0] += extraX;
  position[2] += extraZ;

  uint32_t count = g_tickLogCounter.load(std::memory_order_relaxed);
  if (count % 20 == 0) {
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "position fallback actor=%p actual=%.3f base=%.3f extra=(%.3f,%.3f)",
        actor, actual, baseMovement, extraX, extraZ);
  }
}

bool isMinecraftAddress(uintptr_t address) {
  Dl_info info{};
  if (dladdr(reinterpret_cast<void *>(address), &info) == 0) {
    return true;
  }
  return info.dli_fname == nullptr ||
         std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

void baseTickDetour(void *actor) {
  void *localPlayer = g_localPlayer.load(std::memory_order_relaxed);
  const bool isLocal = localPlayer && actor == localPlayer;
  if (isLocal) {
    applySpeed(actor);
  }
  if (g_baseTickOriginal) {
    g_baseTickOriginal(actor);
  }
  if (isLocal) {
    applyPositionFallback(actor);
  }
}

bool installBaseTickHook(void *localPlayer) {
  if (g_baseTickHookInstalled) {
    return true;
  }
  if (g_baseTickHookAttempted || !localPlayer) {
    return false;
  }
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
  if (pl::memory::hook((pl::memory::FuncPtr)baseTick,
                       (pl::memory::FuncPtr)baseTickDetour,
                       (pl::memory::FuncPtr *)&g_baseTickOriginal) != 0) {
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
      void *previous = g_localPlayer.exchange(actor, std::memory_order_acq_rel);
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

  if (!g_baseTickHookInstalled) {
    applySpeed(actor);
  }
  if (g_normalTickOriginal) {
    g_normalTickOriginal(actor);
  }
  if (!g_baseTickHookInstalled) {
    applyPositionFallback(actor);
  }
}

void *clientInstanceUpdateDetour(void *clientInstance, bool value) {
  void *result = g_clientInstanceUpdateOriginal
                     ? g_clientInstanceUpdateOriginal(clientInstance, value)
                     : nullptr;

  if (clientInstance && g_getLocalPlayer) {
    void *localPlayer = g_getLocalPlayer(clientInstance);
    g_clientLocalPlayer.store(localPlayer, std::memory_order_release);
    if (localPlayer) {
      void *previous = g_localPlayer.exchange(localPlayer, std::memory_order_acq_rel);
      if (localPlayer != previous) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "local player=%p", localPlayer);
      }
      installBaseTickHook(localPlayer);
    }
  }

  return result;
}

bool initialize() {
  std::lock_guard<std::mutex> lock(g_initMutex);
  if (g_installed) {
    return true;
  }

  const auto now = std::chrono::steady_clock::now();
  if (g_lastInitAttempt.time_since_epoch().count() != 0 &&
      now - g_lastInitAttempt < std::chrono::seconds(2)) {
    return false;
  }
  g_lastInitAttempt = now;

  const uintptr_t normalTickAddress =
      pl::memory::resolveSignature(kNormalTickSignature, kMinecraftLibrary);
  const uintptr_t clientUpdateAddress =
      pl::memory::resolveSignature(kClientInstanceUpdateSignature, kMinecraftLibrary);
  const uintptr_t localPlayerAddress =
      pl::memory::resolveSignature(kClientInstanceGetLocalPlayerSignature, kMinecraftLibrary);

  if (!normalTickAddress || !clientUpdateAddress || !localPlayerAddress) {
    logWarning("Unable to resolve required Minecraft signatures");
    return false;
  }

  g_normalTickAddress = normalTickAddress;

  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "1.26.60 signatures: normalTick=%p clientUpdate=%p getLocalPlayer=%p",
                      reinterpret_cast<void *>(normalTickAddress),
                      reinterpret_cast<void *>(clientUpdateAddress),
                      reinterpret_cast<void *>(localPlayerAddress));

  if (!g_getLocalPlayer) {
    g_getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(localPlayerAddress);
  }

  if (!g_clientHookInstalled) {
    if (pl::memory::hook((pl::memory::FuncPtr)clientUpdateAddress,
                         (pl::memory::FuncPtr)clientInstanceUpdateDetour,
                         (pl::memory::FuncPtr *)&g_clientInstanceUpdateOriginal) != 0) {
      logWarning("Unable to hook ClientInstance::update");
      return false;
    }
    g_clientHookInstalled = true;
  }

  if (!g_tickHookInstalled) {
    if (pl::memory::hook((pl::memory::FuncPtr)normalTickAddress,
                         (pl::memory::FuncPtr)normalTickDetour,
                         (pl::memory::FuncPtr *)&g_normalTickOriginal) != 0) {
      logWarning("Unable to hook LocalPlayer::normalTick");
      return false;
    }
    g_tickHookInstalled = true;
  }

  g_installed = true;
  logInfo("Speed module initialized");
  return true;
}

} // namespace

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
  if (multiplier < 1.0f) {
    multiplier = 1.0f;
  } else if (multiplier > 5.0f) {
    multiplier = 5.0f;
  }
  g_multiplier.store(multiplier, std::memory_order_relaxed);
}
