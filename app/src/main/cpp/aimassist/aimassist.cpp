#include <jni.h>

#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <random>

#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"
#include "pl/memory/Vtable.hpp"

namespace {

constexpr const char *kTag = "AimAssist";
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

constexpr const char *kFetchNearbyActorsSignature =
    "? ? ? D1 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? A9 ? ? ? 91 "
    "5B D0 3B D5 F3 03 08 AA F5 03 02 2A";

constexpr const char *kActorIsPlayerSignature =
    "? ? ? F9 ? ? ? 52 ? ? ? 72 ? ? ? A9 08 01 0B CB ? ? ? D3 ? ? ? 51 0C 01 0A 8A "
    "? ? ? B9 6B 0D 0C 8B ? ? ? F9 ? ? ? F9 ? ? ? B1 ? ? ? 54 8B 15 0B 8B ? ? ? B9 "
    "BF 01 0A 6B ? ? ? 54 ? ? ? F9 7F 01 09 EB ? ? ? 54 ? ? ? F9 ? ? ? B4 ? ? ? A9 "
    "? ? ? D3 6B 01 09 CB 5F 0D 8B EB ? ? ? 54 ? ? ? F8 ? ? ? B4 ? ? ? 92 ? ? ? 12 "
    "? ? ? 92 ? ? ? B8 28 01 08 4A ? ? ? 12 1F 01 09 6B E0 27 9F 1A C0 03 5F D6 "
    "E0 03 1F 2A C0 03 5F D6 ? ? ? D1 ? ? ? FD";

constexpr uintptr_t kActorStateVectorComponent = 0x228;
constexpr uintptr_t kActorRotationComponent = 0x238;
constexpr uintptr_t kStatePosition = 0;

// Actor vtable slots (1.26.x, Itanium ABI, indices from the address point).
// attack(Actor&) is a virtual on Actor overridden by Player.
constexpr size_t kActorAttackVtableSlot = 141;
// Mob::swing() is a virtual on Mob; Player/LocalPlayer inherit it.
constexpr size_t kMobSwingVtableSlot = 176;

constexpr float kEyeHeight = 1.62f;
constexpr float kTargetCenterOffset = 0.9f;
constexpr float kDefaultRange = 8.0f;
constexpr float kMinRange = 3.0f;
constexpr float kMaxRange = 16.0f;
constexpr float kRadiansToDegrees = 57.2957795f;
constexpr float kFovPreferenceDegrees = 100.0f;
constexpr float kLegitFovDegrees = 35.0f;
constexpr float kAimDeadzoneDegrees = 0.05f;
constexpr float kLegitDeadzoneDegrees = 1.5f;
constexpr float kMaxTurnSpeedDegreesPerSecond = 1800.0f;
constexpr float kLegitTurnSpeedMin = 340.0f;
constexpr float kLegitTurnSpeedMax = 520.0f;
constexpr float kTriggerFovDegrees = 4.0f;
constexpr float kTriggerMaxDistance = 5.0f;
constexpr int kTriggerDefaultDelayMs = 250;
constexpr int kTriggerMinDelayMs = 120;
constexpr int kTriggerMaxDelayMs = 400;
constexpr int kActorTypeMob = 0x100;

struct Vec2 {
  float pitch;
  float yaw;
};

struct Vec3 {
  float x;
  float y;
  float z;
};

struct DistanceSortedActor {
  void *actor;
  float distance;
  float pad;
};

struct ActorVec {
  DistanceSortedActor *begin;
  DistanceSortedActor *end;
  DistanceSortedActor *cap;
};

using NormalTickFn = void (*)(void *);
using ClientInstanceUpdateFn = void *(*)(void *, bool);
using GetLocalPlayerFn = void *(*)(void *);
using FetchNearbyActorsFn = ActorVec (*)(void *, const Vec3 *, int);
using ActorIsPlayerFn = bool (*)(void *);
using ActorAttackFn = bool (*)(void *, void *);
using MobSwingFn = void (*)(void *);

NormalTickFn g_normalTickOriginal = nullptr;
ClientInstanceUpdateFn g_clientInstanceUpdateOriginal = nullptr;
GetLocalPlayerFn g_getLocalPlayer = nullptr;
FetchNearbyActorsFn g_fetchNearbyActors = nullptr;
ActorIsPlayerFn g_actorIsPlayer = nullptr;
ActorAttackFn g_actorAttack = nullptr;
MobSwingFn g_mobSwing = nullptr;

std::atomic_bool g_enabled{false};
std::atomic_bool g_legitMode{true};
std::atomic_bool g_triggerBotEnabled{false};
std::atomic<int> g_triggerDelayMs{kTriggerDefaultDelayMs};
std::atomic_bool g_triggerReady{false};
std::atomic<float> g_range{kDefaultRange};
std::atomic<float> g_strength{100.0f};
std::atomic<void *> g_localPlayer{nullptr};
std::atomic<uint32_t> g_targetLogCounter{0};

std::mutex g_initMutex;
bool g_clientHookInstalled = false;
bool g_tickHookInstalled = false;
bool g_installed = false;
std::chrono::steady_clock::time_point g_lastInitAttempt{};

std::atomic<void *> g_lockedTarget{nullptr};
std::atomic<void *> g_lastTarget{nullptr};
std::atomic<void *> g_triggerTarget{nullptr};
std::chrono::steady_clock::time_point g_targetLockUntil{};
std::chrono::steady_clock::time_point g_lastAimTime{};
std::chrono::steady_clock::time_point g_reactionUntil{};
std::chrono::steady_clock::time_point g_onTargetSince{};
std::chrono::steady_clock::time_point g_nextAttackAt{};

float g_turnSpeed = kLegitTurnSpeedMax;
float g_aimGain = 0.25f;
float g_smoothedPitch = 0.0f;
float g_smoothedYaw = 0.0f;
bool g_hasSmoothed = false;
bool g_aimSettled = false;
std::chrono::steady_clock::time_point g_aimSettledSince{};

thread_local std::mt19937 g_rng{std::random_device{}()};

float randomRange(float min, float max) {
  if (max <= min) {
    return min;
  }
  std::uniform_real_distribution<float> dist(min, max);
  return dist(g_rng);
}

int randomRangeInt(int min, int max) {
  if (max <= min) {
    return min;
  }
  std::uniform_int_distribution<int> dist(min, max);
  return dist(g_rng);
}

void clearTargetState() {
  g_lockedTarget.store(nullptr, std::memory_order_relaxed);
  g_triggerReady.store(false, std::memory_order_relaxed);
  g_lastTarget.store(nullptr, std::memory_order_relaxed);
  g_triggerTarget.store(nullptr, std::memory_order_relaxed);
}

void logInfo(const char *message) {
  __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message);
}

void logWarning(const char *message) {
  __android_log_print(ANDROID_LOG_WARN, kTag, "%s", message);
}

float wrapDegrees(float degrees) {
  degrees = std::fmod(degrees + 180.0f, 360.0f);
  if (degrees < 0.0f) {
    degrees += 360.0f;
  }
  return degrees - 180.0f;
}

bool readPosition(void *actor, Vec3 *out) {
  if (!actor) {
    return false;
  }
  auto *base = reinterpret_cast<char *>(actor);
  auto stateVector = *reinterpret_cast<uintptr_t *>(base + kActorStateVectorComponent);
  if (stateVector < 0x1000 || (stateVector & 0x7) != 0) {
    return false;
  }
  auto *position = reinterpret_cast<float *>(stateVector + kStatePosition);
  if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2])) {
    return false;
  }
  out->x = position[0];
  out->y = position[1];
  out->z = position[2];
  return true;
}

bool readRotation(void *actor, Vec2 *out) {
  if (!actor) {
    return false;
  }
  auto *base = reinterpret_cast<char *>(actor);
  auto rotation = *reinterpret_cast<uintptr_t *>(base + kActorRotationComponent);
  if (rotation < 0x1000 || (rotation & 0x7) != 0) {
    return false;
  }
  auto *values = reinterpret_cast<float *>(rotation);
  if (!std::isfinite(values[0]) || !std::isfinite(values[1])) {
    return false;
  }
  out->pitch = values[0];
  out->yaw = values[1];
  return true;
}

// Camera-only aim: write the final rotation straight into the actor's rotation
// component. One consistent value per tick is serialized into PlayerAuthInput,
// so the server sees vanilla-looking movement and the client never fights
// itself (no shake).
bool writeRotation(void *actor, float pitch, float yaw) {
  if (!actor) {
    return false;
  }
  auto *base = reinterpret_cast<char *>(actor);
  auto rotation = *reinterpret_cast<uintptr_t *>(base + kActorRotationComponent);
  if (rotation < 0x1000 || (rotation & 0x7) != 0) {
    return false;
  }
  auto *values = reinterpret_cast<float *>(rotation);
  if (!std::isfinite(pitch) || !std::isfinite(yaw)) {
    return false;
  }
  if (pitch > 90.0f) pitch = 90.0f;
  if (pitch < -90.0f) pitch = -90.0f;
  values[0] = pitch;
  values[1] = yaw;
  return true;
}

bool isMinecraftAddress(uintptr_t address) {
  if (!address) {
    return false;
  }
  Dl_info info{};
  if (dladdr(reinterpret_cast<void *>(address), &info) == 0 || !info.dli_fname) {
    return false;
  }
  const char *name = info.dli_fname;
  for (const char *p = name; *p; ++p) {
    if (p[0] == 'l' && p[1] == 'i' && p[2] == 'b' && p[3] == 'm' &&
        p[4] == 'i' && p[5] == 'n' && p[6] == 'e') {
      return true;
    }
  }
  return false;
}

void *resolveVtableSlot(void *instance, size_t slot) {
  if (!instance) {
    return nullptr;
  }
  auto **vtable = *reinterpret_cast<void ***>(instance);
  if (!vtable) {
    return nullptr;
  }
  void *fn = vtable[slot];
  if (!isMinecraftAddress(reinterpret_cast<uintptr_t>(fn))) {
    return nullptr;
  }
  return fn;
}

void resolveAttackFunctions(void *localPlayer) {
  if (!localPlayer || (g_actorAttack && g_mobSwing)) {
    return;
  }
  if (!g_actorAttack) {
    void *fn = resolveVtableSlot(localPlayer, kActorAttackVtableSlot);
    if (fn) {
      g_actorAttack = reinterpret_cast<ActorAttackFn>(fn);
      __android_log_print(ANDROID_LOG_INFO, kTag, "Player::attack resolved=%p slot=%zu",
                          fn, kActorAttackVtableSlot);
    }
  }
  if (!g_mobSwing) {
    void *fn = resolveVtableSlot(localPlayer, kMobSwingVtableSlot);
    if (fn) {
      g_mobSwing = reinterpret_cast<MobSwingFn>(fn);
      __android_log_print(ANDROID_LOG_INFO, kTag, "Mob::swing resolved=%p slot=%zu",
                          fn, kMobSwingVtableSlot);
    }
  }
}

struct AimCandidate {
  void *actor = nullptr;
  float distance = 0.0f;
  float angular = 0.0f;
  float desiredPitch = 0.0f;
  float desiredYaw = 0.0f;
};

void aimAtLocalPlayer(void *localPlayer) {
  if (!localPlayer || localPlayer != g_localPlayer.load(std::memory_order_acquire) ||
      !g_fetchNearbyActors || !g_actorIsPlayer) {
    return;
  }

  const bool aimEnabled = g_enabled.load(std::memory_order_relaxed);
  const bool triggerEnabled = g_triggerBotEnabled.load(std::memory_order_relaxed);
  if (!aimEnabled && !triggerEnabled) {
    return;
  }

  resolveAttackFunctions(localPlayer);

  Vec3 eye{};
  if (!readPosition(localPlayer, &eye)) {
    clearTargetState();
    return;
  }
  Vec2 current{};
  if (!readRotation(localPlayer, &current)) {
    clearTargetState();
    return;
  }
  eye.y += kEyeHeight;

  const float range = g_range.load(std::memory_order_relaxed);
  Vec3 extent{range, range, range};
  ActorVec nearby = g_fetchNearbyActors(localPlayer, &extent, kActorTypeMob);
  if (!nearby.begin || !nearby.end || nearby.end <= nearby.begin) {
    clearTargetState();
    return;
  }
  if (nearby.end - nearby.begin > 512) {
    nearby.end = nearby.begin + 512;
  }

  AimCandidate bestFov{};
  AimCandidate bestNear{};
  AimCandidate lockedCandidate{};
  bool haveFov = false;
  bool haveNear = false;
  bool haveLocked = false;
  void *lockedTarget = g_lockedTarget.load(std::memory_order_relaxed);
  const bool legit = g_legitMode.load(std::memory_order_relaxed);
  const float selectionFov = legit ? kLegitFovDegrees : kFovPreferenceDegrees;

  for (DistanceSortedActor *it = nearby.begin; it < nearby.end; ++it) {
    void *actor = it->actor;
    if (!actor || actor == localPlayer) {
      continue;
    }
    if (!g_actorIsPlayer(actor)) {
      continue;
    }

    Vec3 target{};
    if (!readPosition(actor, &target)) {
      continue;
    }

    const float dx = target.x - eye.x;
    const float dy = (target.y + kTargetCenterOffset) - eye.y;
    const float dz = target.z - eye.z;
    const float horizontal = std::sqrt(dx * dx + dz * dz);
    const float distance = std::sqrt(horizontal * horizontal + dy * dy);
    if (!std::isfinite(distance) || distance < 0.1f || distance > range) {
      continue;
    }

    const float desiredYaw = std::atan2(-dx, dz) * kRadiansToDegrees;
    const float desiredPitch = -std::atan2(dy, horizontal) * kRadiansToDegrees;
    if (!std::isfinite(desiredYaw) || !std::isfinite(desiredPitch)) {
      continue;
    }

    const float deltaYaw = wrapDegrees(desiredYaw - current.yaw);
    const float deltaPitch = wrapDegrees(desiredPitch - current.pitch);
    const float angular = std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);

    AimCandidate candidate{};
    candidate.actor = actor;
    candidate.distance = distance;
    candidate.angular = angular;
    candidate.desiredPitch = desiredPitch;
    candidate.desiredYaw = desiredYaw;

    if (!haveNear || distance < bestNear.distance) {
      bestNear = candidate;
      haveNear = true;
    }
    if (angular <= selectionFov && (!haveFov || angular < bestFov.angular)) {
      bestFov = candidate;
      haveFov = true;
    }
    if (actor == lockedTarget) {
      lockedCandidate = candidate;
      haveLocked = true;
    }
  }

  const auto now = std::chrono::steady_clock::now();
  const float switchMargin = legit ? 12.0f : 8.0f;
  const auto lockDuration = std::chrono::milliseconds(legit ? 900 : 600);
  const AimCandidate *target = nullptr;
  if (haveLocked && now < g_targetLockUntil &&
      (!legit || lockedCandidate.angular <= kLegitFovDegrees)) {
    target = &lockedCandidate;
    if (haveFov && bestFov.angular + switchMargin < lockedCandidate.angular) {
      target = &bestFov;
      g_lockedTarget.store(bestFov.actor, std::memory_order_relaxed);
      g_targetLockUntil = now + lockDuration;
    }
  } else if (haveFov) {
    target = &bestFov;
    g_lockedTarget.store(bestFov.actor, std::memory_order_relaxed);
    g_targetLockUntil = now + lockDuration;
  } else if (!legit && haveNear) {
    target = &bestNear;
    g_lockedTarget.store(bestNear.actor, std::memory_order_relaxed);
    g_targetLockUntil = now + lockDuration;
  }

  if (!target) {
    clearTargetState();
    g_hasSmoothed = false;
    g_aimSettled = false;
    return;
  }

  if (target->actor != g_lastTarget.load(std::memory_order_relaxed)) {
    g_lastTarget.store(target->actor, std::memory_order_relaxed);
    g_triggerReady.store(false, std::memory_order_relaxed);
    g_smoothedPitch = current.pitch;
    g_smoothedYaw = current.yaw;
    g_hasSmoothed = true;
    g_aimSettled = false;
    g_turnSpeed = legit ? randomRange(kLegitTurnSpeedMin, kLegitTurnSpeedMax)
                        : kMaxTurnSpeedDegreesPerSecond;
    g_aimGain = legit ? randomRange(0.16f, 0.22f) : 1.0f;
    g_reactionUntil = now + std::chrono::milliseconds(
        legit ? randomRangeInt(140, 280) : 0);
  }

  float desiredPitch = target->desiredPitch;
  float desiredYaw = target->desiredYaw;
  if (legit && g_hasSmoothed) {
    const float alpha = 0.45f;
    g_smoothedPitch += wrapDegrees(desiredPitch - g_smoothedPitch) * alpha;
    g_smoothedYaw += wrapDegrees(desiredYaw - g_smoothedYaw) * alpha;
    desiredPitch = g_smoothedPitch;
    desiredYaw = g_smoothedYaw;
  }

  const float deltaYaw = wrapDegrees(desiredYaw - current.yaw);
  const float deltaPitch = wrapDegrees(desiredPitch - current.pitch);
  const float targetDeltaYaw = wrapDegrees(target->desiredYaw - current.yaw);
  const float targetDeltaPitch = wrapDegrees(target->desiredPitch - current.pitch);
  const float targetAngular =
      std::sqrt(targetDeltaYaw * targetDeltaYaw + targetDeltaPitch * targetDeltaPitch);
  const float settleThreshold = 1.5f;
  if (std::fabs(targetDeltaYaw) <= settleThreshold &&
      std::fabs(targetDeltaPitch) <= settleThreshold) {
    if (!g_aimSettled) {
      g_aimSettled = true;
      g_aimSettledSince = now;
    }
  } else {
    g_aimSettled = false;
  }

  // --- Trigger bot: native attack through the game's own attack/swing path ---
  const bool onTarget =
      targetAngular <= kTriggerFovDegrees && target->distance <= kTriggerMaxDistance;
  const bool settled = g_aimSettled &&
                       now - g_aimSettledSince >= std::chrono::milliseconds(150);

  if (triggerEnabled && onTarget && (aimEnabled ? settled : true)) {
    g_triggerTarget.store(target->actor, std::memory_order_relaxed);
    if (g_onTargetSince.time_since_epoch().count() == 0) {
      g_onTargetSince = now;
    }
    const int delayMs = g_triggerDelayMs.load(std::memory_order_relaxed);
    const bool ready = now - g_onTargetSince >= std::chrono::milliseconds(delayMs);
    g_triggerReady.store(ready, std::memory_order_relaxed);

    if (ready && now >= g_nextAttackAt) {
      const int jitter = randomRangeInt(40, std::max(120, delayMs));
      g_nextAttackAt = now + std::chrono::milliseconds(delayMs + jitter);
      if (randomRangeInt(0, 99) >= 15) {
        // Real attack: the game emits the InventoryTransaction attack packet and
        // the vanilla arm swing animation itself. No synthetic input events.
        if (g_mobSwing) {
          g_mobSwing(localPlayer);
        }
        if (g_actorAttack) {
          g_actorAttack(localPlayer, target->actor);
        }
      }
    }
  } else {
    g_triggerReady.store(false, std::memory_order_relaxed);
    g_triggerTarget.store(nullptr, std::memory_order_relaxed);
    g_onTargetSince = std::chrono::steady_clock::time_point{};
  }

  if (!aimEnabled) {
    return;
  }

  if (legit && now < g_reactionUntil) {
    return;
  }

  const float deadzone = legit ? kLegitDeadzoneDegrees : kAimDeadzoneDegrees;
  if (std::fabs(deltaYaw) < deadzone && std::fabs(deltaPitch) < deadzone) {
    return;
  }

  float strength = std::max(
      0.05f, g_strength.load(std::memory_order_relaxed) / 100.0f);
  if (legit) {
    strength = std::max(strength, 0.35f);
  }

  float dt = 0.05f;
  if (g_lastAimTime.time_since_epoch().count() != 0) {
    dt = std::chrono::duration<float>(now - g_lastAimTime).count();
  }
  g_lastAimTime = now;
  if (!std::isfinite(dt) || dt < 0.001f) {
    dt = 0.001f;
  } else if (dt > 0.1f) {
    dt = 0.1f;
  }

  float stepYaw = 0.0f;
  float stepPitch = 0.0f;
  if (legit) {
    const float maxStep = g_turnSpeed * strength * dt;
    stepYaw = std::clamp(deltaYaw * g_aimGain, -maxStep, maxStep);
    stepPitch = std::clamp(deltaPitch * g_aimGain, -maxStep, maxStep);
    if (std::fabs(stepYaw) < 0.02f) stepYaw = 0.0f;
    if (std::fabs(stepPitch) < 0.02f) stepPitch = 0.0f;
  } else {
    const float maxStep = kMaxTurnSpeedDegreesPerSecond * strength * dt;
    stepYaw = std::clamp(deltaYaw, -maxStep, maxStep);
    stepPitch = std::clamp(deltaPitch, -maxStep, maxStep);
  }

  if (stepYaw == 0.0f && stepPitch == 0.0f) {
    return;
  }

  const float newPitch = current.pitch + stepPitch;
  const float newYaw = wrapDegrees(current.yaw + stepYaw);
  writeRotation(localPlayer, newPitch, newYaw);

  uint32_t count = g_targetLogCounter.fetch_add(1, std::memory_order_relaxed);
  if (count % 30 == 0) {
    Vec2 after{};
    readRotation(localPlayer, &after);
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "target=%p dist=%.2f desired=(pitch %.2f, yaw %.2f) current=(pitch %.2f, yaw %.2f) "
        "step=(pitchUp %.2f, yaw %.2f) after=(pitch %.2f, yaw %.2f) strength=%.0f",
        target->actor, target->distance, target->desiredPitch, target->desiredYaw,
        current.pitch, current.yaw, -stepPitch, stepYaw, after.pitch, after.yaw,
        g_strength.load(std::memory_order_relaxed));
  }
}

void normalTickDetour(void *actor) {
  if (g_normalTickOriginal) {
    g_normalTickOriginal(actor);
  }
  if (actor && actor == g_localPlayer.load(std::memory_order_acquire)) {
    aimAtLocalPlayer(actor);
  }
}

void *clientInstanceUpdateDetour(void *clientInstance, bool value) {
  void *result = g_clientInstanceUpdateOriginal
                     ? g_clientInstanceUpdateOriginal(clientInstance, value)
                     : nullptr;

  if (clientInstance && g_getLocalPlayer) {
    void *localPlayer = g_getLocalPlayer(clientInstance);
    if (localPlayer) {
      void *previous = g_localPlayer.exchange(localPlayer, std::memory_order_acq_rel);
      if (previous != localPlayer) {
        __android_log_print(ANDROID_LOG_INFO, kTag, "local player=%p", localPlayer);
        resolveAttackFunctions(localPlayer);
      }
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
  const uintptr_t fetchNearbyActorsAddress =
      pl::memory::resolveSignature(kFetchNearbyActorsSignature, kMinecraftLibrary);
  const uintptr_t actorIsPlayerAddress =
      pl::memory::resolveSignature(kActorIsPlayerSignature, kMinecraftLibrary);

  if (!normalTickAddress || !clientUpdateAddress || !localPlayerAddress ||
      !fetchNearbyActorsAddress || !actorIsPlayerAddress) {
    logWarning("Unable to resolve required Minecraft signatures");
    return false;
  }

  g_getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(localPlayerAddress);
  g_fetchNearbyActors = reinterpret_cast<FetchNearbyActorsFn>(fetchNearbyActorsAddress);
  g_actorIsPlayer = reinterpret_cast<ActorIsPlayerFn>(actorIsPlayerAddress);

  __android_log_print(
      ANDROID_LOG_INFO, kTag,
      "1.26.60 signatures: normalTick=%p clientUpdate=%p getLocalPlayer=%p "
      "fetchNearby=%p isPlayer=%p",
      reinterpret_cast<void *>(normalTickAddress),
      reinterpret_cast<void *>(clientUpdateAddress),
      reinterpret_cast<void *>(localPlayerAddress),
      reinterpret_cast<void *>(fetchNearbyActorsAddress),
      reinterpret_cast<void *>(actorIsPlayerAddress));

  if (!g_tickHookInstalled) {
    if (pl::memory::hook((pl::memory::FuncPtr)normalTickAddress,
                         (pl::memory::FuncPtr)normalTickDetour,
                         (pl::memory::FuncPtr *)&g_normalTickOriginal) != 0) {
      logWarning("Unable to hook LocalPlayer::normalTick");
      return false;
    }
    g_tickHookInstalled = true;
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

  g_installed = true;
  logInfo("Aim assist module initialized");
  return true;
}

} // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeInit(
    JNIEnv *, jclass) {
  return initialize() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetEnabled(
    JNIEnv *, jclass, jboolean enabled) {
  const bool value = enabled == JNI_TRUE;
  g_enabled.store(value, std::memory_order_relaxed);
  if (!value) {
    clearTargetState();
  }
  __android_log_print(ANDROID_LOG_INFO, kTag, "enabled=%d", value ? 1 : 0);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeIsEnabled(
    JNIEnv *, jclass) {
  return g_enabled.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetRange(
    JNIEnv *, jclass, jfloat range) {
  if (!std::isfinite(range)) {
    return;
  }
  if (range < kMinRange) range = kMinRange;
  if (range > kMaxRange) range = kMaxRange;
  g_range.store(range, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetStrength(
    JNIEnv *, jclass, jfloat strength) {
  if (!std::isfinite(strength)) {
    return;
  }
  if (strength < 0.0f) strength = 0.0f;
  if (strength > 100.0f) strength = 100.0f;
  g_strength.store(strength, std::memory_order_relaxed);
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetLegitMode(
    JNIEnv *, jclass, jboolean enabled) {
  g_legitMode.store(enabled == JNI_TRUE, std::memory_order_relaxed);
  clearTargetState();
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTriggerBot(
    JNIEnv *, jclass, jboolean enabled) {
  g_triggerBotEnabled.store(enabled == JNI_TRUE, std::memory_order_relaxed);
  if (enabled != JNI_TRUE) {
    g_triggerReady.store(false, std::memory_order_relaxed);
    g_triggerTarget.store(nullptr, std::memory_order_relaxed);
    g_onTargetSince = std::chrono::steady_clock::time_point{};
  }
}

extern "C" JNIEXPORT void JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeSetTriggerDelay(
    JNIEnv *, jclass, jint delayMs) {
  if (delayMs < kTriggerMinDelayMs) delayMs = kTriggerMinDelayMs;
  if (delayMs > kTriggerMaxDelayMs) delayMs = kTriggerMaxDelayMs;
  g_triggerDelayMs.store(static_cast<int>(delayMs), std::memory_order_relaxed);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_levimc_launcher_core_mods_inbuilt_nativemod_AimAssistMod_nativeIsOnTarget(
    JNIEnv *, jclass) {
  return g_triggerReady.load(std::memory_order_relaxed) ? JNI_TRUE : JNI_FALSE;
}
