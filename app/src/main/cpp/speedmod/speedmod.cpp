#include <jni.h>

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

namespace {

constexpr const char *kTag = "SpeedMod";
constexpr const char *kMinecraftLibrary = "libminecraftpe.so";

constexpr const char *kNormalTickSignature =
    "E8 0F 19 FC FD 7B 01 A9 FC 6F 02 A9 FA 67 03 A9 F8 5F 04 A9 F6 57 05 A9 "
    "F4 4F 06 A9 FD 43 00 91 FF C3 09 D1 54 D0 3B D5 F3 03 00 AA 88 16 40 F9 "
    "A8 03 1E F8 08 C0 78 39 ? ? ? ? 68 92 4C B9";

constexpr const char *kClientInstanceUpdateSignature =
    "FD 7B BA A9 FC 6F 01 A9 FA 67 02 A9 F8 5F 03 A9 F6 57 04 A9 F4 4F 05 A9 "
    "FD 03 00 91 FF C3 12 D1 59 D0 3B D5 F3 03 00 AA F4 03 01 2A 28 17 40 F9 "
    "A8 83 1F F8 08 00 40 F9 09 35 46 F9 E8 E3 01 91";

constexpr const char *kClientInstanceGetLocalPlayerSignature =
    "FF 43 01 D1 FD 7B 03 A9 F3 23 00 F9 FD C3 00 91 53 D0 3B D5 E8 03 00 AA "
    "E0 23 00 91 69 16 40 F9 01 61 08 91 A9 83 1F F8 ? ? ? ? E0 23 00 91 ? ? ? ? "
    "? ? ? ? E0 23 00 91 21 00 80 52 ? ? ? ? ? ? ? ? E0 03 1F AA 68 16 40 F9 "
    "A9 83 5F F8 1F 01 09 EB ? ? ? ? FD 7B 43 A9";

constexpr uintptr_t kActorStateVectorComponent = 0x208;
constexpr uintptr_t kStatePosition = 0;
constexpr uintptr_t kStatePreviousPosition = 12;
constexpr uintptr_t kStatePositionDelta = 24;
constexpr float kMaxExtraBlocksPerTick = 0.75f;

using NormalTickFn = void (*)(void *);
using ClientInstanceUpdateFn = void *(*)(void *, bool);
using GetLocalPlayerFn = void *(*)(void *);

NormalTickFn g_normalTickOriginal = nullptr;
ClientInstanceUpdateFn g_clientInstanceUpdateOriginal = nullptr;
GetLocalPlayerFn g_getLocalPlayer = nullptr;

std::atomic_bool g_enabled{false};
std::atomic<float> g_multiplier{2.0f};
std::atomic<void *> g_localPlayer{nullptr};

std::mutex g_initMutex;
bool g_clientHookInstalled = false;
bool g_tickHookInstalled = false;
bool g_installed = false;
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

void applySpeed(void *actor) {
  if (!actor || !g_enabled.load(std::memory_order_relaxed)) {
    return;
  }

  void *localPlayer = g_localPlayer.load(std::memory_order_relaxed);
  if (!localPlayer || actor != localPlayer) {
    return;
  }

  auto *base = reinterpret_cast<char *>(actor);
  auto stateVector = *reinterpret_cast<uintptr_t *>(base + kActorStateVectorComponent);
  if (stateVector < 0x1000 || (stateVector & 0x7) != 0) {
    return;
  }

  auto *position = reinterpret_cast<float *>(stateVector + kStatePosition);
  auto *previous = reinterpret_cast<float *>(stateVector + kStatePreviousPosition);
  auto *delta = reinterpret_cast<float *>(stateVector + kStatePositionDelta);

  constexpr float kWorldLimit = 3.0e7f;
  if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2]) || std::fabs(position[0]) > kWorldLimit ||
      std::fabs(position[1]) > kWorldLimit || std::fabs(position[2]) > kWorldLimit) {
    return;
  }

  float dx = delta[0];
  float dz = delta[2];
  if (!std::isfinite(dx) || !std::isfinite(dz)) {
    return;
  }
  if (horizontalLength(dx, dz) < 1.0e-5f) {
    dx = position[0] - previous[0];
    dz = position[2] - previous[2];
  }

  const float movement = horizontalLength(dx, dz);
  if (movement < 1.0e-5f || movement > 2.0f) {
    return;
  }

  float multiplier = g_multiplier.load(std::memory_order_relaxed);
  if (multiplier <= 1.0f) {
    return;
  }

  float extraX = dx * (multiplier - 1.0f);
  float extraZ = dz * (multiplier - 1.0f);
  const float extraLength = horizontalLength(extraX, extraZ);
  if (extraLength > kMaxExtraBlocksPerTick) {
    const float scale = kMaxExtraBlocksPerTick / extraLength;
    extraX *= scale;
    extraZ *= scale;
  }

  position[0] += extraX;
  position[2] += extraZ;
}

void normalTickDetour(void *actor) {
  if (g_normalTickOriginal) {
    g_normalTickOriginal(actor);
  }
  applySpeed(actor);
}

void *clientInstanceUpdateDetour(void *clientInstance, bool value) {
  void *result = g_clientInstanceUpdateOriginal
                     ? g_clientInstanceUpdateOriginal(clientInstance, value)
                     : nullptr;

  if (clientInstance && g_getLocalPlayer) {
    g_localPlayer.store(g_getLocalPlayer(clientInstance), std::memory_order_release);
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
  g_enabled.store(enabled == JNI_TRUE, std::memory_order_relaxed);
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
