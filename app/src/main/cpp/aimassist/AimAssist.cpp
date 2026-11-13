#include "AimAssist.hpp"

#include <android/log.h>

#include "AntiBot.hpp"
#include "DiagLog.hpp"
#include "Esp.hpp"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>

#include "GameEcs.hpp"
#include "Offsets.hpp"
#include "CombatState.hpp"

namespace aimbot {
namespace {

constexpr const char *kTag = "AimAssist";
constexpr float kPi = 3.14159265358979323846f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kMaxDt = 0.05f;

float clampf(float value, float minValue, float maxValue) {
  return std::max(minValue, std::min(maxValue, value));
}

int64_t monotonicMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

float normalizeYaw(float yaw) {
  yaw = std::fmod(yaw + 180.0f, 360.0f);
  if (yaw < 0.0f) yaw += 360.0f;
  return yaw - 180.0f;
}

float shortestDelta(float current, float target) {
  return std::fmod(target - current + 540.0f, 360.0f) - 180.0f;
}

int64_t nowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Направление взгляда из углов Bedrock: pitch < 0 — вверх, yaw > 0 — вправо.
Vec3 directionFromAngles(const ViewAngles &view) {
  const float yaw = view.yaw * kDegToRad;
  const float pitch = view.pitch * kDegToRad;
  const float cp = std::cos(pitch);
  Vec3 dir{};
  dir.x = -std::sin(yaw) * cp;
  dir.y = -std::sin(pitch);
  dir.z = std::cos(yaw) * cp;
  return dir;
}

// Пересечение луча с AABB (slab method). Возвращает ближайшее t в [0, maxT].
bool rayIntersectsAabb(const Vec3 &origin, const Vec3 &dir, const Aabb &box,
                       float maxT, float &hitT) {
  float tMin = 0.0f;
  float tMax = maxT;
  const float o[3] = {origin.x, origin.y, origin.z};
  const float d[3] = {dir.x, dir.y, dir.z};
  const float lo[3] = {box.min.x, box.min.y, box.min.z};
  const float hi[3] = {box.max.x, box.max.y, box.max.z};

  for (int axis = 0; axis < 3; ++axis) {
    if (std::fabs(d[axis]) < 1e-6f) {
      if (o[axis] < lo[axis] || o[axis] > hi[axis]) return false;
      continue;
    }
    const float inv = 1.0f / d[axis];
    float t1 = (lo[axis] - o[axis]) * inv;
    float t2 = (hi[axis] - o[axis]) * inv;
    if (t1 > t2) std::swap(t1, t2);
    if (t1 > tMin) tMin = t1;
    if (t2 < tMax) tMax = t2;
    if (tMin > tMax) return false;
  }

  hitT = tMin;
  return tMin <= maxT;
}


} // namespace

AimAssist &AimAssist::instance() {
  static AimAssist module;
  return module;
}

// ============================================================
// ModuleBase
// ============================================================
void AimAssist::onEnable() {
  m_hasLocal = false;
  m_velYaw = 0.0f;
  m_velPitch = 0.0f;
  m_target = {};
  m_targetHistory.clear();
  m_historyActor = nullptr;
  m_stickyLeft = 0;
  m_targetGrace = 0;
  m_frameCounter = 0;
  m_lastStepYaw = 0.0f;
  m_lastStepPitch = 0.0f;
  m_lastDelta = 0.0f;
  m_lastFactor = 0.0f;
  m_deadzoneHit = false;
  m_aimEngageAtMs = 0;
  m_targetLockMs = 0;
  m_lastAimActor = nullptr;
  m_jitterYaw = 0.0f;
  m_jitterPitch = 0.0f;
  m_lastSwitchMs = 0;
  m_triggerDelayForTargetMs = 0;
  m_fatigueScale = 1.0f;
  m_nextFatigueMs = 0;
  m_fatigueUntilMs = 0;
  m_tickAccumYaw = 0.0f;
  m_tickAccumPitch = 0.0f;
  m_lastTickYaw = 0.0f;
  m_lastTickPitch = 0.0f;
  m_prevTickDeltaDeg = 0.0f;
  m_haveTickSample = false;
  m_lastNetTickMs = 0;
  m_serverViewSinceMs = 0;
  m_flickTicksLeft = 0;
  m_haveEyeSample = false;
  m_lastEyeY = 0.0f;
  m_lastEyeMs = 0;
  m_vertVel = 0.0f;
  m_fallingSinceMs = 0;
  resetHumanProfile();
  m_fastAimTurnApplied = false;
  // Per-session reseed: a fixed seed gives an identical noise trace every
  // launch, which is easy for server-side ML to fingerprint.
  m_rng.seed(static_cast<std::minstd_rand::result_type>(
      std::chrono::steady_clock::now().time_since_epoch().count() ^
      reinterpret_cast<uintptr_t>(this)));
  m_onTarget.store(false, std::memory_order_release);
  // Release-публикация после сброса состояния: render-поток увидит
  // согласованные поля, а не полу-инициализированные.
  m_enabled.store(true, std::memory_order_release);
  // Fast aim стартует разворот в тот же кадр, без ожидания тика поиска.
  const Settings bootSettings = getSettings();
  if (bootSettings.fastAim) {
    void *player = m_bridge.getLocalPlayer();
    if (player) findTargetInternal(player, bootSettings);
  }
  aimbot::diagLog(kTag, "module enabled");
}

void AimAssist::onDisable() {
  m_enabled.store(false, std::memory_order_release);
  m_bridge.setSilentAim(false, 0.0f, 0.0f);
  clearTarget();
  m_velYaw = 0.0f;
  m_velPitch = 0.0f;
  m_onTarget.store(false, std::memory_order_release);
  aimbot::diagLog(kTag, "module disabled");
}

void AimAssist::onTick() { updateNetwork(); }

void AimAssist::onRender(float dt) { updateVisual(dt); }

void AimAssist::onConfigLoad() {
  // Здесь можно вызвать конфиг-систему клиента.
  // В текущем APK настройки приходят через setRange/setStrength/setLegitMode.
  m_hasLocal = false;
  m_velYaw = 0.0f;
  m_velPitch = 0.0f;
  clearTarget();
}

// ============================================================
// Настройки
// ============================================================
Settings AimAssist::getSettings() const {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  return m_settings;
}

void AimAssist::setSettings(const Settings &settings) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings = settings;
}

void AimAssist::setEnabled(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.enabled = enabled;
  }
  if (enabled) {
    onEnable();
  } else {
    onDisable();
  }
}

bool AimAssist::isEnabled() const {
  return m_enabled.load(std::memory_order_acquire);
}

void AimAssist::setRange(float range) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.range = clampf(range, 3.0f, 20.0f);
}

void AimAssist::setStrength(float strength) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.strength = clampf(strength, 0.0f, 100.0f);
}

void AimAssist::setFov(float fovDeg) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.fovDeg = clampf(fovDeg, 1.0f, 360.0f);
}

void AimAssist::setWallCheck(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.visibleCheck = enabled;
}

void AimAssist::setIgnoreInvisible(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.ignoreInvisible = enabled;
}

void AimAssist::setTargetPlayers(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.targetPlayers = enabled;
}

void AimAssist::setTargetMobs(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.targetMobs = enabled;
}

void AimAssist::setBodyPart(int bodyPart) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  switch (std::max(0, std::min(4, bodyPart))) {
  case 1:
    m_settings.targetBone = TargetBone::Chest;
    break;
  case 2:
    m_settings.targetBone = TargetBone::Legs;
    break;
  case 3:
    m_settings.targetBone = TargetBone::UpperChest;
    break;
  case 4:
    m_settings.targetBone = TargetBone::LowerChest;
    break;
  default:
    m_settings.targetBone = TargetBone::Head;
    break;
  }
}

void AimAssist::setMaxSpeed(float degPerSecond) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.maxDegPerSecond = clampf(degPerSecond, 60.0f, 2000.0f);
}

void AimAssist::setSmoothing(float speed) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.smoothingSpeed = clampf(speed, 1.0f, 60.0f);
}

void AimAssist::setAcceleration(float degPerSecond2) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.maxAccelDegPerSecond2 = clampf(degPerSecond2, 100.0f, 12000.0f);
}

void AimAssist::setDeadzone(float deg) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.deadzoneDeg = clampf(deg, 0.0f, 5.0f);
}

void AimAssist::setFullSpeedRange(float deg) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.fullSpeedRangeDeg = clampf(deg, 1.0f, 90.0f);
}

void AimAssist::setStickyTicks(int ticks) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.stickyTicks = std::max(0, std::min(40, ticks));
}

void AimAssist::setTargetLock(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.targetLock = enabled;
  if (!enabled) {
    m_stickyLeft = 0;
  }
}

void AimAssist::setFastAim(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.fastAim = enabled;
    if (enabled) {
      m_settings.targetPriority = TargetPriority::Angle;
    }
  }
  if (enabled && m_enabled.load(std::memory_order_acquire)) {
    // Не ждём следующего кадра: захватываем цель сразу.
    void *player = m_bridge.getLocalPlayer();
    if (player) findTargetInternal(player, getSettings());
  }
}

void AimAssist::setFastAimTime(float timeMs) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.fastAimTimeMs = clampf(timeMs, 20.0f, 200.0f);
}

void AimAssist::setFastAimMaxSpeed(float degPerSecond) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.fastAimMaxSpeed = clampf(degPerSecond, 120.0f, 4000.0f);
}

void AimAssist::setTargetGrace(int frames) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.targetGraceFrames = std::max(0, std::min(60, frames));
}

void AimAssist::setPreferClosest(bool preferClosest) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.targetPriority = preferClosest ? TargetPriority::Distance
                                            : TargetPriority::Angle;
}

void AimAssist::setLegitMode(bool legit) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.legit = legit;
  if (legit) {
    m_settings.smoothingSpeed = 60.0f;
    m_settings.maxDegPerSecond = 2200.0f;
    m_settings.maxAccelDegPerSecond2 = 12000.0f;
    m_settings.deadzoneDeg = 0.0f;
    m_settings.fullSpeedRangeDeg = 60.0f;
    m_settings.targetSmoothFrames = 0;
  } else {
    m_settings.smoothingSpeed = 70.0f;
    m_settings.maxDegPerSecond = 2800.0f;
    m_settings.maxAccelDegPerSecond2 = 16000.0f;
    m_settings.deadzoneDeg = 0.0f;
    m_settings.fullSpeedRangeDeg = 70.0f;
    m_settings.targetSmoothFrames = 0;
  }
}

void AimAssist::setTriggerBot(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerBot = enabled;
  m_triggerSinceActor = nullptr;
  m_triggerSinceMs = 0;
  m_triggerLastSeenMs = 0;
  m_triggerDelayForTargetMs = 0;
  m_nextAttackAtMs = 0;
  if (!enabled) {
    m_onTarget.store(false, std::memory_order_release);
  }
  aimbot::diagLog(kTag, "setTriggerBot(%d)",
                      enabled ? 1 : 0);
}

void AimAssist::setTriggerDelay(int delayMs) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  // 0 is valid: PvP needs the hit on the first frame the crosshair lands.
  m_settings.triggerDelayMs = std::max(0, std::min(400, delayMs));
}

void AimAssist::setTriggerInterval(int intervalMs) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerIntervalMs = std::max(150, std::min(2000, intervalMs));
}

void AimAssist::setTriggerRange(float range) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerRange = std::max(2.0f, std::min(5.0f, range));
}

void AimAssist::setTriggerTolerance(float tolerance) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerBoxTolerance = std::max(0.0f, std::min(0.25f, tolerance));
}

void AimAssist::setHumanization(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.humanize = enabled;
  if (!enabled) {
    m_aimEngageAtMs = 0;
    m_lastAimActor = nullptr;
    m_targetLockMs = 0;
    resetHumanProfile();
  }
}

void AimAssist::setReactionDelay(int baseMs, int jitterMs) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.reactionDelayMs =
      static_cast<float>(std::max(0, std::min(600, baseMs)));
  m_settings.reactionJitterMs =
      static_cast<float>(std::max(0, std::min(400, jitterMs)));
}

void AimAssist::setAimTremor(float degrees) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.aimTremorDeg = std::max(0.0f, std::min(0.5f, degrees));
}

void AimAssist::setAntiCheatMode(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.antiCheatMode = enabled;
}

void AimAssist::setAssistCone(float degrees) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.assistConeDeg = clampf(degrees, 10.0f, 360.0f);
}

void AimAssist::setAttackMissChance(float chance) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.attackMissChance = clampf(chance, 0.0f, 0.35f);
}

void AimAssist::setServerViewGuard(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.serverViewGuard = enabled;
}

void AimAssist::setSustainedTickDelta(float degrees) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.sustainedTickDeltaDeg = clampf(degrees, 4.0f, 40.0f);
}

void AimAssist::setOnlyCrit(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.onlyCrit = enabled;
  if (enabled) {
    m_fallingSinceMs = 0;
  }
}

void AimAssist::setAntiBot(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.antiBot = enabled;
  }
  AntiBot::instance().setEnabled(enabled);
}

void AimAssist::setRandomAimPoint(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.randomAimPoint = enabled;
}

void AimAssist::setNeuralRotation(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.neuralRotation = enabled;
}

void AimAssist::setKillaura(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    m_settings.killaura = enabled;
  }
  if (!enabled) {
    m_bridge.setSilentAim(false, 0.0f, 0.0f);
  }
}

void AimAssist::setKillauraConeDeg(float deg) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.killauraConeDeg = clampf(deg, 10.0f, 360.0f);
}

void AimAssist::setKillauraReactionMs(float ms) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.killauraReactionMs = clampf(ms, 0.0f, 600.0f);
}

void AimAssist::setRotationRecording(bool enabled) {
  {
    std::lock_guard<std::mutex> lock(m_settingsMutex);
    if (enabled && !m_rotRecording) {
      m_rotPattern.clear();
      m_rotPlayIndex = 0;
      m_havePrevLocal = false;
    }
    m_rotRecording = enabled;
  }
  if (!enabled) saveRotationPattern();
}

void AimAssist::setRotationPatternPath(const std::string &path) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_rotPatternPath = path;
  loadRotationPattern();
}

void AimAssist::saveRotationPattern() {
  if (m_rotPatternPath.empty()) return;
  std::ofstream out(m_rotPatternPath, std::ios::binary | std::ios::trunc);
  if (!out) return;
  const uint32_t count = static_cast<uint32_t>(m_rotPattern.size());
  out.write(reinterpret_cast<const char *>(&count), sizeof(count));
  if (count) {
    out.write(reinterpret_cast<const char *>(m_rotPattern.data()),
              static_cast<std::streamsize>(count) * sizeof(float));
  }
}

void AimAssist::loadRotationPattern() {
  if (m_rotPatternPath.empty()) return;
  std::ifstream in(m_rotPatternPath, std::ios::binary);
  if (!in) return;
  uint32_t count = 0;
  in.read(reinterpret_cast<char *>(&count), sizeof(count));
  if (!in || count == 0 || count > 500000) return;
  m_rotPattern.resize(count);
  in.read(reinterpret_cast<char *>(m_rotPattern.data()),
          static_cast<std::streamsize>(count) * sizeof(float));
  if (!in) m_rotPattern.clear();
  m_rotPlayIndex = 0;
}

void AimAssist::setTouchActive(bool) {
  // Ручной look больше не ставит наведение на паузу. View всё равно
  // перечитывается каждый кадр, поэтому цель не теряется.
}

void AimAssist::notifyCameraLook() {
  // Наведение не должно замолкать, пока игрок двигает камеру.
  // Фактический view читается в updateVisual каждый кадр.
}

void AimAssist::setActivationHeld(bool held) {
  m_activationHeld.store(held, std::memory_order_release);
}

bool AimAssist::isOnTarget() const {
  return m_onTarget.load(std::memory_order_acquire);
}

DebugState AimAssist::getDebugState() const {
  DebugState state{};
  const Settings settings = getSettings();
  state.enabled = m_enabled.load(std::memory_order_acquire);
  state.hasTarget = m_target.valid;
  state.onTarget = m_onTarget.load(std::memory_order_acquire);
  state.deadzoneHit = m_deadzoneHit;
  state.freezeNetwork = settings.freezeNetwork;
  state.localPitch = m_localPitch.load(std::memory_order_relaxed);
  state.localYaw = m_localYaw.load(std::memory_order_relaxed);
  state.targetPitch = m_target.pitch;
  state.targetYaw = m_target.yaw;
  state.delta = m_lastDelta;
  state.factor = m_lastFactor;
  state.dt = m_lastDt;
  state.target = m_target.actor;
  state.targetDistance = m_target.distance;
  return state;
}

// ============================================================
// Активность / пауза
// ============================================================
bool AimAssist::isActive(const Settings &settings) const {
  if (!settings.enabled) return false;
  switch (settings.activationMode) {
  case ActivationMode::Always:
    return true;
  case ActivationMode::Key:
  case ActivationMode::Attack:
    return m_activationHeld.load(std::memory_order_acquire);
  }
  return false;
}

// ============================================================
// Основной визуальный апдейт (каждый кадр)
// ============================================================
void AimAssist::updateVisual(float dt) {
  dt = clampf(dt, 0.0f, kMaxDt);
  m_lastDt = dt;

  static uint64_t s_visualCalls = 0;
  const uint64_t visualCall = ++s_visualCalls;

  void *player = m_bridge.getLocalPlayer();
  if (visualCall <= 3 || (visualCall % 3600) == 0) {
    aimbot::diagLog(kTag, "updateVisual#%llu client=%p player=%p",
                    static_cast<unsigned long long>(visualCall),
                    m_bridge.getClientInstance(), player);
  }
  if (player != m_lastLocalPlayer) {
    // World exit/enter or session change: the previous target actor belongs
    // to a different Level and may already be freed.
    m_lastLocalPlayer = player;
    clearTarget();
    m_bridge.setSilentAim(false, 0.0f, 0.0f);
    m_triggerSinceActor = nullptr;
    m_triggerSinceMs = 0;
  }
  if (!player) {
    clearTarget();
    m_onTarget.store(false, std::memory_order_release);
    m_triggerActor.store(nullptr, std::memory_order_release);
    static uint64_t s_noPlayer = 0;
    if ((++s_noPlayer % 240) == 1) {
      aimbot::diagLog(kTag,
                          "updateVisual: no local player (client=%p)",
                          m_bridge.getClientInstance());
    }
    return;
  }

  ViewAngles current{};
  if (!m_bridge.getViewAngles(player, current)) {
    m_onTarget.store(false, std::memory_order_release);
    m_triggerActor.store(nullptr, std::memory_order_release);
    static uint64_t s_noAngles = 0;
    if ((++s_noAngles % 240) == 1) {
      aimbot::diagLog(kTag, "updateVisual: getViewAngles failed (player=%p)",
                      player);
    }
    return;
  }

  Vec3 eyeNow{};
  const bool haveEye = m_bridge.getEyePosition(player, eyeNow);
  if (haveEye) {
    m_eye = eyeNow;
    // Вертикальная скорость локального игрока — нужна для only crit.
    // Считаем по низу хитбокса (ноги), а не по глазам: приседание меняет
    // высоту глаз и давало ложное «падение».
    Aabb localBounds{};
    if (m_bridge.getActorBounds(player, localBounds)) {
      updateVerticalMotion(localBounds.min.y, nowMs());
    }
  }

  const Settings settings = getSettings();

  // ActorRotationComponent — фактический view камеры. Синхронизируем local
  // каждый кадр, чтобы ручное движение и ходьба не оставляли устаревший угол.
  m_localPitch.store(current.pitch, std::memory_order_relaxed);
  m_localYaw.store(current.yaw, std::memory_order_relaxed);
  m_hasLocal = true;


  // ---------------------------------------------------------------
  // Триггер: работает всегда, даже когда aim assist выключен.
  // Решение — пересечение луча прицела с хитбоксом игрока/моба,
  // а не угол до цели, выбранной aim assist.
  // ---------------------------------------------------------------
  const float actorRange =
      std::max(settings.range, settings.triggerRange + 0.5f);
  if (settings.triggerBot || m_enabled.load(std::memory_order_acquire)) {
    // Aim assist needs a fresh list every frame for responsive acquisition.
    m_bridge.fillNearbyActors(player, actorRange, m_actors);
  }
  if (settings.antiBot) {
    AntiBot::instance().update(m_actors, player);
  }
  if (settings.killaura) {
    updateKillaura(player, settings, dt);
    return;
  }
  if (settings.triggerBot && haveEye) {
    updateTrigger(player, settings, current, eyeNow, dt);
  } else {
    m_onTarget.store(false, std::memory_order_release);
    m_triggerActor.store(nullptr, std::memory_order_release);
    m_triggerDistance.store(0.0f, std::memory_order_relaxed);
  }

  // Диагностика: раз в ~60 кадров пишем состояние и реальный dt.
  // Помогает понять, где именно обрывается цепочка на устройстве и с какой
  // частотой игра вызывает ClientInstance::update (от неё зависит плавность).
  static uint64_t s_diagFrame = 0;
  static float s_diagDtSum = 0.0f;
  static int s_diagDtCount = 0;
  s_diagDtSum += dt;
  ++s_diagDtCount;
  if ((++s_diagFrame % 300) == 0) {
    const float avgDt = s_diagDtSum / static_cast<float>(
        std::max(1, s_diagDtCount));
    s_diagDtSum = 0.0f;
    s_diagDtCount = 0;
    aimbot::diagLog(kTag,
        "diag t=%lld player=%p view=(%.1f,%.1f) eye=(%.2f,%.2f,%.2f) "
        "actors=%zu trigger=%d range=%.2f target=%p onTarget=%d gm=%p "
        "dt=%.4f fps=%.1f fast=%d legit=%d smooth=%.1f fastTime=%.1f "
        "maxSpeed=%.0f strength=%.0f",
        static_cast<long long>(nowMs()), player,
        static_cast<double>(current.pitch),
        static_cast<double>(current.yaw), static_cast<double>(eyeNow.x),
        static_cast<double>(eyeNow.y), static_cast<double>(eyeNow.z),
        m_actors.size(), settings.triggerBot ? 1 : 0,
        static_cast<double>(settings.triggerRange),
        m_triggerActor.load(std::memory_order_acquire),
        m_onTarget.load(std::memory_order_acquire) ? 1 : 0,
        m_bridge.getGameMode(),
        static_cast<double>(avgDt),
        static_cast<double>(avgDt > 1e-5f ? 1.0f / avgDt : 0.0f),
        settings.fastAim ? 1 : 0, settings.legit ? 1 : 0,
        static_cast<double>(settings.smoothingSpeed),
        static_cast<double>(settings.fastAimTimeMs),
        static_cast<double>(settings.fastAimMaxSpeed),
        static_cast<double>(settings.strength));
  }

  if (!m_enabled.load(std::memory_order_acquire)) {
    return;
  }

  if (!isActive(settings)) {
    clearTarget();
    return;
  }

  // Поиск цели каждый кадр: иначе цель, прошедшая рядом/сбоку, теряется
  // на несколько кадров. Список актёров переиспользует capacity.
  m_frameCounter++;
  findTargetInternal(player, settings, true);

  if (!m_target.valid) {
    // Цели нет: не двигаем камеру вообще. Скорость гасим плавно,
    // чтобы при появлении цели не было накопленного импульса.
    m_velYaw *= 0.85f;
    m_velPitch *= 0.85f;
    m_lastAimActor = nullptr;
    m_aimEngageAtMs = 0;
    m_targetLockMs = 0;
    m_reachFrames = 0;
    resetHumanProfile();
    return;
  }

  // Высота цели задаётся режимом: голова / грудь / ноги.
  m_target.point = getTargetPoint(m_target.actor, settings.targetBone,
                                settings.triggerBot);
  const Angles latest = calculateAnglesToPoint(m_target.point);
  m_target.yaw = latest.yaw;
  m_target.pitch = latest.pitch;
  const float dxTarget = m_target.point.x - m_eye.x;
  const float dyTarget = m_target.point.y - m_eye.y;
  const float dzTarget = m_target.point.z - m_eye.z;
  m_target.distance = std::sqrt(dxTarget * dxTarget + dyTarget * dyTarget +
                                dzTarget * dzTarget);
  if (m_target.distance > settings.range + 1.5f) {
    clearTarget();
    return;
  }

  const int64_t aimNow = nowMs();

  // Target ESP живёт на валидной цели AimAssist, а не только на ударе:
  // иначе без TriggerBot модуль всегда пустой.
  Esp::instance().markTargetEspHit(m_target.actor);

  // Fast aim: без человеческого профиля и EMA позиции. Чистая экспонента
  // плюс лимит скорости и бюджет на серверный тик.
  if (settings.fastAim) {
    if (m_lastAimActor != m_target.actor) {
      m_lastAimActor = m_target.actor;
      // PvP: engage almost immediately. The old 45-165 ms reaction plus
      // random pauses lost the first hit of every fight.
      m_aimEngageAtMs = aimNow + randomInt(0, 25);
      m_fastTimeScale = randomFloat(0.85f, 1.12f);
      m_fastSpeedScale = randomFloat(0.9f, 1.1f);
      m_fastStealthScale = randomFloat(0.65f, 1.35f);
      m_nextFastPauseMs = aimNow + 60000; // pauses disabled for PvP
      m_fastPauseUntilMs = 0;
      resetHumanProfile();
      // New fight, new small placement: a human never holds the exact same
      // spot on the hitbox. Amplitude is well under 0.2 deg, so it does not
      // change the perceived speed or add tremor.
      m_placeYaw = randomFloat(-1.0f, 1.0f) * settings.restOffsetDeg;
      m_placePitch = randomFloat(-0.7f, 0.7f) * settings.restOffsetDeg;
      m_sinePhase1 = randomFloat(0.0f, 6.2831853f);
      m_sinePhase2 = randomFloat(0.0f, 6.2831853f);
      m_sineFreq1 = randomFloat(0.55f, 1.6f);
      m_sineFreq2 = randomFloat(1.2f, 2.8f);
      m_reachFrames = 0;
      m_reachStartMs = 0;
      m_reachMaxStep = 0.0f;
    }
    if (aimNow < m_aimEngageAtMs) {
      m_fastAimTurnApplied = false;
      return;
    }
    m_targetLockMs = aimNow;
    m_fastAimTurnApplied = false;
    stabilizeFastAimAngles(dt);
    updateFastAimStealth(settings, dt);
    // Slow sinusoidal sway (sub-0.1 deg) plus the per-fight placement offset.
    // It is smooth by construction, so it breaks perfect-tracking statistics
    // without introducing the high-frequency tremor of random noise.
    const float fastDt = clampf(dt, 0.001f, 0.05f);
    m_sinePhase1 += fastDt * 6.2831853f * m_sineFreq1;
    m_sinePhase2 += fastDt * 6.2831853f * m_sineFreq2;
    if (m_sinePhase1 > 6.2831853f) m_sinePhase1 -= 6.2831853f;
    if (m_sinePhase2 > 6.2831853f) m_sinePhase2 -= 6.2831853f;
    const float swayAmp = clampf(settings.trackingNoiseDeg, 0.0f, 0.08f);
    const float swayYaw =
        (std::sin(m_sinePhase1) + 0.5f * std::sin(m_sinePhase2)) * swayAmp;
    const float swayPitch =
        (std::sin(m_sinePhase1 * 1.13f + 1.7f) +
         0.5f * std::sin(m_sinePhase2 * 0.87f)) * swayAmp * 0.75f;
    const float aimYaw = m_target.yaw + m_placeYaw + swayYaw;
    const float aimPitch = m_target.pitch + m_placePitch + swayPitch;
    const float beforeYaw = m_localYaw.load(std::memory_order_relaxed);
    const float beforePitch = m_localPitch.load(std::memory_order_relaxed);
    const float preErrYaw = shortestDelta(beforeYaw, aimYaw);
    const float preErrPitch = aimPitch - beforePitch;
    const float preErr =
        std::sqrt(preErrYaw * preErrYaw + preErrPitch * preErrPitch);
    applySmoothRotationInternal(aimYaw, aimPitch, dt, settings);
    const float afterYaw = m_localYaw.load(std::memory_order_relaxed);
    const float afterPitch = m_localPitch.load(std::memory_order_relaxed);
    m_lastStepYaw = shortestDelta(beforeYaw, afterYaw);
    m_lastStepPitch = afterPitch - beforePitch;
    m_lastDelta = std::sqrt(m_lastStepYaw * m_lastStepYaw +
                            m_lastStepPitch * m_lastStepPitch);
    // One log line per engagement (not per frame): a per-frame file write
    // opened/closed the log 45 times in the middle of every approach, which
    // itself caused frame hitches.
    if (m_reachFrames == 0) {
      m_reachStartMs = aimNow;
      m_reachStartErr = preErr;
      m_reachMaxStep = 0.0f;
    }
    ++m_reachFrames;
    m_reachMaxStep = std::max(m_reachMaxStep, m_lastDelta);
    if (preErr < 0.5f || m_reachFrames >= 60) {
      const int64_t durMs =
          m_reachStartMs != 0 ? aimNow - m_reachStartMs : 0;
      aimbot::diagLog(kTag,
          "reach frames=%d dur=%lldms startErr=%.2f endErr=%.2f "
          "maxStep=%.2f fps=%.1f fastTime=%.1f legit=%d",
          m_reachFrames, static_cast<long long>(durMs),
          static_cast<double>(m_reachStartErr),
          static_cast<double>(preErr),
          static_cast<double>(m_reachMaxStep),
          static_cast<double>(m_lastDt > 1e-5f ? 1.0f / m_lastDt : 0.0f),
          static_cast<double>(settings.fastAimTimeMs),
          settings.legit ? 1 : 0);
      m_reachFrames = 0;
    }
    if (settings.debugOverlay) {
      aimbot::diagLog(kTag,
          "fast aim target=%p step=(p %.3f y %.3f) dt=%.4f",
          m_target.actor, static_cast<double>(m_lastStepPitch),
          static_cast<double>(m_lastStepYaw),
          static_cast<double>(m_lastDt));
    }
    return;
  }

  // Человеческая реакция: на новую цель камера выходит не мгновенно.
  // Триггер от этого не зависит и продолжает работать во время задержки.
  if (settings.humanize) {
    if (m_lastAimActor != m_target.actor) {
      m_lastAimActor = m_target.actor;
      // Новая цель — новый моторный профиль. Ошибки/смещения прошлой цели
      // не переносятся, иначе переключение выглядит как «телепорт» точки.
      resetHumanProfile();
      int reactionJitter = 0;
      if (settings.reactionJitterMs > 0.0f) {
        reactionJitter =
            randomInt(0, static_cast<int>(settings.reactionJitterMs));
      }
      m_aimEngageAtMs = aimNow +
                        static_cast<int64_t>(
                            std::max(0.0f, settings.reactionDelayMs)) +
                        reactionJitter;
    }
    if (aimNow < m_aimEngageAtMs) {
      // Держим камеру неподвижно, но не копим скорость.
      m_velYaw *= 0.6f;
      m_velPitch *= 0.6f;
      return;
    }
  } else {
    if (m_lastAimActor != m_target.actor) {
      resetHumanProfile();
    }
    m_lastAimActor = m_target.actor;
    m_aimEngageAtMs = 0;
  }

  const Vec3 point = smoothedPoint(m_target, settings);
  Angles aim = calculateAnglesToPoint(point);

  if (m_targetLockMs == 0) {
    m_targetLockMs = aimNow;
  }

  if (settings.humanize) {
    updateHumanProfile(settings, dt, aimNow);
    updateFatigue(settings, aimNow);
  }

  // Окно «флика»: быстрый поворот разрешён только сразу после захвата
  // цели или при большой ошибке. Постоянное сопровождение на 500°/с —
  // сильный маркер ML-моделей.
  {
    const float curYaw = m_localYaw.load(std::memory_order_relaxed);
    const float curPitch = m_localPitch.load(std::memory_order_relaxed);
    const float errYaw = shortestDelta(curYaw, aim.yaw);
    const float errPitch = aim.pitch - curPitch;
    const float err = std::sqrt(errYaw * errYaw + errPitch * errPitch);
    if (m_targetLockMs == aimNow || err > 25.0f) {
      m_flickTicksLeft =
          std::max(m_flickTicksLeft, settings.flickTickBudget);
    }
  }

  const float beforeYaw = m_localYaw.load(std::memory_order_relaxed);
  const float beforePitch = m_localPitch.load(std::memory_order_relaxed);
  applySmoothRotationInternal(aim.yaw, aim.pitch, dt, settings);
  const float afterYaw = m_localYaw.load(std::memory_order_relaxed);
  const float afterPitch = m_localPitch.load(std::memory_order_relaxed);

  const float stepYaw = shortestDelta(beforeYaw, afterYaw);
  const float stepPitch = afterPitch - beforePitch;
  if (std::isfinite(stepYaw) && std::isfinite(stepPitch) &&
      std::fabs(stepYaw) < 45.0f && std::fabs(stepPitch) < 45.0f &&
      (std::fabs(stepYaw) > 0.0001f || std::fabs(stepPitch) > 0.0001f)) {
    // Только applyTurnDelta. Никаких записей rotation-компонента в кадре:
    // именно они давали feedback-петлю и дрожание камеры.
    m_bridge.applyTurnDelta(player, -stepPitch, stepYaw);
  }
  m_lastStepYaw = stepYaw;
  m_lastStepPitch = stepPitch;

  // m_onTarget здесь не трогаем: его владелец — updateTrigger (ray-vs-hitbox).

  if (settings.debugOverlay) {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastDebug.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDebug)
                .count() >= 1000) {
      m_lastDebug = now;
      aimbot::diagLog(kTag,
          "local=(p %.2f y %.2f) target=(p %.2f y %.2f) delta=%.2f "
          "factor=%.3f step=(p %.3f y %.3f) dt=%.4f deadzone=%d target=%p",
          afterPitch, afterYaw, m_target.pitch, m_target.yaw, m_lastDelta,
          m_lastFactor, m_lastStepPitch, m_lastStepYaw, m_lastDt,
          m_deadzoneHit ? 1 : 0, m_target.actor);
    }
  }
}

// ============================================================
// Сетевой тик 20 Гц
// ============================================================
void AimAssist::updateNetwork() {
  const float localYaw = m_localYaw.load(std::memory_order_relaxed);
  const float localPitch = m_localPitch.load(std::memory_order_relaxed);
  // Это ровно та ротация, которую сервер получит в ближайшем
  // PlayerAuthInput/tick. Храним её отдельно от локальной камеры.
  m_networkYaw.store(localYaw, std::memory_order_relaxed);
  m_networkPitch.store(localPitch, std::memory_order_relaxed);

  const Settings settings = getSettings();
  const int64_t tickNow = monotonicMs();
  if (!m_haveTickSample) {
    m_lastTickYaw = localYaw;
    m_lastTickPitch = localPitch;
    m_prevTickDeltaDeg = 0.0f;
    m_haveTickSample = true;
  } else {
    const float tickDeltaYaw = shortestDelta(m_lastTickYaw, localYaw);
    const float tickDeltaPitch = localPitch - m_lastTickPitch;
    const float tickMagnitude =
        std::sqrt(tickDeltaYaw * tickDeltaYaw + tickDeltaPitch * tickDeltaPitch);
    m_lastTickYaw = localYaw;
    m_lastTickPitch = localPitch;
    m_prevTickDeltaDeg = tickMagnitude;

    static uint64_t s_netTicks = 0;
    const uint64_t netTicks = ++s_netTicks;
    if (settings.debugOverlay && (netTicks % 20) == 1) {
      aimbot::diagLog(kTag,
                      "net tick dy=%.2f dp=%.2f |d|=%.2f flick=%d "
                      "age=%lldms",
                      static_cast<double>(tickDeltaYaw),
                      static_cast<double>(tickDeltaPitch),
                      static_cast<double>(tickMagnitude), m_flickTicksLeft,
                      static_cast<long long>(tickNow - m_lastNetTickMs));
    }
  }
  m_lastNetTickMs = tickNow;
  m_tickAccumYaw = 0.0f;
  m_tickAccumPitch = 0.0f;
  if (m_flickTicksLeft > 0) {
    --m_flickTicksLeft;
  }

  if (m_stickyLeft > 0) {
    --m_stickyLeft;
  }

  if (settings.freezeNetwork || !m_enabled.load(std::memory_order_acquire)) {
    return;
  }

  void *player = m_bridge.getLocalPlayer();
  if (!player) return;

  // Body yaw — только 20 Гц и только если явно включено.
  // В 1.26.x applyTurnDelta обычно уже обновляет rotation-компонент.
  if (settings.syncBodyYaw) {
    m_bridge.setBodyYaw(player, localYaw);
  }

  // В 1.26.x локальные углы сами попадают в PlayerAuthInput.
  // Если нужен явный MovePlayerPacket — подключи sender через
  // ClientBridge::setMovePacketSender().
  m_bridge.sendMovePacket(player, localYaw, localPitch);
}

// ============================================================
// Поиск цели
// ============================================================
void AimAssist::findTarget() {
  void *player = m_bridge.getLocalPlayer();
  if (!player) return;
  const Settings settings = getSettings();
  findTargetInternal(player, settings);
}

void AimAssist::findTargetInternal(void *player, const Settings &settings,
                                   bool actorsAlreadyFilled) {
  if (!player) return;

  Vec3 eye{};
  if (!m_bridge.getEyePosition(player, eye)) {
    clearTarget();
    return;
  }
  m_eye = eye;

  if (!actorsAlreadyFilled) {
    m_bridge.fillNearbyActors(player, settings.range, m_actors);
    if (settings.antiBot) {
      AntiBot::instance().update(m_actors, player);
    }
  }

  Target bestAngle{};
  Target bestDistance{};
  bestAngle.angle = FLT_MAX;
  bestDistance.distance = FLT_MAX;

  // Для выбора цели берём фактический view, а не local: если applyTurnDelta
  // и компонент камеры разошлись, старая логика «не видела» игрока прямо
  // под прицелом и теряла цель при проходе сбоку/сзади.
  ViewAngles view{};
  const bool hasView = m_bridge.getViewAngles(player, view);
  const float localYaw =
      hasView ? view.yaw : m_localYaw.load(std::memory_order_relaxed);
  const float localPitch =
      hasView ? view.pitch : m_localPitch.load(std::memory_order_relaxed);

  // Жёсткий конус захвата даже при FOV=360. Развороты на 90-180° —
  // самый явный маркер aimbot/aimassist для серверных эвристик.
  // Fast aim с anti-cheat профилем не разворачивается за спину: снапы
  // на 90-180° — первый маркер rotation-check'ов. Без профиля — 360°.
  const float assistFov =
      settings.killaura
          ? std::min(settings.fovDeg, settings.killauraConeDeg)
          : (settings.antiCheatMode
                 ? std::min(settings.fovDeg, settings.assistConeDeg)
                 : settings.fovDeg);

  Target current = m_target;
  bool currentStillValid = false;

  // Актёр, в чей хитбокс сейчас упирается прицел (посчитан updateTrigger).
  // Его aim assist обязан вести первым: иначе доводка утащит камеру с
  // хитбокса и триггер перестанет бить.
  // Whoever the crosshair ray is actually on is the aim target, fast aim or
  // not. Previously this priority was disabled with fastAim enabled, so the
  // assist could pull the crosshair off the enemy TriggerBot was hitting and
  // then swing to another actor - the goal jumps read as teleports.
  void *hitActor = settings.triggerBot
                       ? m_triggerActor.load(std::memory_order_acquire)
                       : nullptr;
  Target triggerTarget{};

  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;
    const bool playerActor = m_bridge.isPlayer(actor);
    if (playerActor ? !settings.targetPlayers : !settings.targetMobs) continue;
    if (settings.ignoreInvisible && isInvisible(actor)) continue;
    if (settings.teamCheck && isSameTeam(player, actor)) continue;
    if (playerActor && Esp::instance().isFriend(actor)) continue;
    if (playerActor && settings.antiBot &&
        AntiBot::instance().isBot(actor)) {
      continue;
    }

    Vec3 targetPosition{};
    if (!m_bridge.getPosition(actor, targetPosition)) continue;

    const Vec3 point = getTargetPoint(actor, settings.targetBone, settings.triggerBot);
    const Angles angles = calculateAnglesToPoint(point);

    const float deltaYaw = shortestDelta(localYaw, angles.yaw);
    const float deltaPitch = angles.pitch - localPitch;
    const float angular =
        std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);
    const float dx = point.x - eye.x;
    const float dy = point.y - eye.y;
    const float dz = point.z - eye.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);

    if (distance > settings.range) continue;
    if (assistFov < 359.5f && angular > assistFov) continue;

    // The actor under the crosshair is already line-of-sight checked by
    // TriggerBot against its actual hit point; a chest point can be behind a
    // half-block while the head/arm is visible, and dropping the hit actor
    // here made the assist swing away mid-fight.
    if (settings.visibleCheck && actor != hitActor &&
        !m_bridge.hasLineOfSight(player, eye, point)) {
      continue;
    }

    Target candidate{};
    candidate.actor = actor;
    candidate.point = point;
    candidate.distance = distance;
    candidate.angle = angular;
    candidate.yaw = angles.yaw;
    candidate.pitch = angles.pitch;
    candidate.valid = true;

    if (hitActor && actor == hitActor) {
      triggerTarget = candidate;
    }

    if (actor == current.actor) {
      current = candidate;
      currentStillValid = true;
    }

    if (candidate.angle < bestAngle.angle) {
      bestAngle = candidate;
    }
    if (candidate.distance < bestDistance.distance) {
      bestDistance = candidate;
    }
  }

  // Hard engagement lock. While the current target is alive, visible and
  // inside targetLockRange, keep aiming at it even if it left the FOV cone
  // (e.g. ran behind the player) and even if another actor is under the
  // crosshair. This both stops accidental target stealing and makes the
  // camera turn around when an opponent runs behind the back.
  if (settings.targetLock && m_target.valid && m_target.actor) {
    const bool playerActor = m_bridge.isPlayer(m_target.actor);
    const bool allowed =
        playerActor ? settings.targetPlayers : settings.targetMobs;
    const float lockRange = clampf(settings.targetLockRange, 3.0f, 20.0f);
    const Vec3 lockPoint =
        getTargetPoint(m_target.actor, settings.targetBone, true);
    Vec3 lockPos{};
    const bool havePos = m_bridge.getPosition(m_target.actor, lockPos);
    if (allowed && havePos) {
      const float dx = lockPoint.x - eye.x;
      const float dy = lockPoint.y - eye.y;
      const float dz = lockPoint.z - eye.z;
      const float lockDist = std::sqrt(dx * dx + dy * dy + dz * dz);
      const bool alive = gameecs::readHealth(m_target.actor) > 0.05f;
      const bool los =
          !settings.visibleCheck ||
          m_bridge.hasLineOfSight(player, eye, lockPoint);
      if (alive && lockDist <= lockRange && los) {
        const Angles a = calculateAnglesToPoint(lockPoint);
        const float ly = shortestDelta(localYaw, a.yaw);
        const float lp = a.pitch - localPitch;
        Target locked{};
        locked.actor = m_target.actor;
        locked.point = lockPoint;
        locked.distance = lockDist;
        locked.yaw = a.yaw;
        locked.pitch = a.pitch;
        locked.angle = std::sqrt(ly * ly + lp * lp);
        locked.valid = true;
        m_target = locked;
        m_target.valid = true;
        m_targetGrace = settings.targetGraceFrames;
        return;
      }
    }
  }

  Target chosen = settings.targetPriority == TargetPriority::Angle
                      ? bestAngle
                      : bestDistance;

  // Приоритет цели триггера: не даём aim assist увести прицел с хитбокса.
  const bool triggerOverride = triggerTarget.valid;
  if (triggerOverride) {
    chosen = triggerTarget;
  }

  // Target lock: удерживаем выбранную цель, пока она валидна. Быстрое
  // переключение остаётся только когда прицел стоит точно на другой цели.
  if (settings.targetLock && currentStillValid && !triggerOverride) {
    // Hard lock while the target is alive and visible. The only legitimate
    // switch is the TriggerBot ray picking a different actor (handled above),
    // or the current target becoming invalid. Angle-based switching between
    // two nearby enemies moved the goal tens of degrees and looked like a
    // teleport even when the per-frame step was small.
    chosen = current;
  }

  bool better = triggerOverride;
  bool directHit = false;
  if (!triggerOverride && currentStillValid && chosen.valid) {
    directHit = chosen.actor != current.actor && chosen.angle <= 1.5f;
    if (settings.targetPriority == TargetPriority::Angle) {
      better = directHit ||
               chosen.angle + settings.switchHysteresisDeg < current.angle;
    } else {
      better = directHit ||
               (chosen.distance + 1.0f < current.distance &&
                chosen.angle <= current.angle + 10.0f);
    }
    // Минимальное время удержания цели. Без него assist перескакивает
    // между двумя близкими целями каждый кадр (multi-aura паттерн).
    const int64_t switchNow = nowMs();
    const int switchDelay = m_currentSwitchDelayMs > 0
                                ? m_currentSwitchDelayMs
                                : settings.switchDelayMs;
    if (!directHit && switchDelay > 0 &&
        m_lastSwitchMs != 0 &&
        switchNow - m_lastSwitchMs < switchDelay) {
      better = false;
    }
  }

  // Sticky target: пока тики не вышли, держим текущую цель, но игрок,
  // оказавшийся прямо под прицелом, перехватывает цель сразу.
  if (currentStillValid && m_stickyLeft > 0 && !better) {
    m_target = current;
    m_target.valid = true;
    m_targetGrace = settings.targetGraceFrames;
    return;
  }

  if (currentStillValid && !better) {
    m_target = current;
    m_target.valid = true;
    m_targetGrace = settings.targetGraceFrames;
    return;
  }

  if (chosen.valid) {
    const bool switched = chosen.actor != m_target.actor;
    m_target = chosen;
    if (switched) {
      // Не переносим скорость с прошлой цели: иначе рывок на переключении.
      m_velYaw = 0.0f;
      m_velPitch = 0.0f;
      m_targetHistory.clear();
      m_historyActor = chosen.actor;
      m_aimEngageAtMs = 0;
      m_lastAimActor = nullptr;
      m_lastSwitchMs = nowMs();
      m_currentSwitchDelayMs =
          std::max(25, settings.switchDelayMs + randomInt(-30, 60));
      m_aimPointActor = nullptr;
      m_nextAimPointMs = 0;
      m_serverViewSinceMs = 0;
      resetHumanProfile();
    }
    m_stickyLeft = std::max(0, settings.stickyTicks);
    m_target.valid = true;
    m_targetGrace = settings.targetGraceFrames;
  } else if (currentStillValid) {
    m_target = current;
    m_target.valid = true;
    m_targetGrace = settings.targetGraceFrames;
  } else if (m_target.valid && m_targetGrace > 0) {
    // Актёр мог выпасть из nearby-списка на кадр при проходе рядом/сзади.
    // Держим последнюю цель ещё несколько кадров, пока позиция читается.
    Vec3 stillThere{};
    if (m_bridge.getPosition(m_target.actor, stillThere)) {
      --m_targetGrace;
      m_target.valid = true;
    } else {
      clearTarget();
    }
  } else {
    clearTarget();
  }
}

// ============================================================
// Триггер: луч прицела против хитбоксов
// ============================================================
void AimAssist::updateKillaura(void *player, const Settings &settings,
                              float dt) {
  if (!player) return;
  const int64_t now = nowMs();
  Vec3 eye{};
  if (!m_bridge.getEyePosition(player, eye)) return;
  m_eye = eye;
  const float reach = settings.range + 0.6f;
  void *best = nullptr;
  float bestDist = 1e9f;
  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;
    const bool isPlayerActor = m_bridge.isPlayer(actor);
    if (isPlayerActor ? !settings.targetPlayers : !settings.targetMobs) continue;
    if (isPlayerActor && Esp::instance().isFriend(actor)) continue;
    if (isPlayerActor && settings.antiBot &&
        AntiBot::instance().isBot(actor)) {
      continue;
    }
    if (settings.ignoreInvisible && isInvisible(actor)) continue;
    Vec3 pos{};
    if (!m_bridge.getPosition(actor, pos)) continue;
    const float dx = pos.x - eye.x;
    const float dy = pos.y - eye.y;
    const float dz = pos.z - eye.z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (distance > reach) continue;
    if (settings.visibleCheck &&
        !m_bridge.hasLineOfSight(player, eye, pos)) {
      continue;
    }
    if (distance < bestDist) {
      bestDist = distance;
      best = actor;
    }
  }

  if (!best) {
    m_bridge.setSilentAim(false, 0.0f, 0.0f);
    m_triggerActor.store(nullptr, std::memory_order_release);
    m_onTarget.store(false, std::memory_order_release);
    m_triggerSinceActor = nullptr;
    m_triggerSinceMs = 0;
    m_lastAimActor = nullptr;
    return;
  }

  m_triggerActor.store(best, std::memory_order_release);
  m_onTarget.store(true, std::memory_order_release);

  const Vec3 point = getTargetPoint(best, settings.targetBone, true);
  const Angles aim = calculateAnglesToPoint(point);
  // SILENT aura: only the outgoing PlayerAuthInput rotation is set to the
  // target; the local camera never moves. The attack is then aligned on the
  // server side, so it registers with no visible aiming.
  m_bridge.setSilentAim(true, aim.pitch, aim.yaw);

  if (m_triggerSinceActor != best) {
    m_triggerSinceActor = best;
    m_triggerSinceMs = now;
    m_triggerDelayForTargetMs =
        static_cast<int>(settings.killauraReactionMs) + randomInt(0, 60);
  }
  if (now - m_triggerSinceMs < m_triggerDelayForTargetMs) return;
  if (m_nextAttackAtMs != 0 && now < m_nextAttackAtMs) return;

  if (m_bridge.attackActor(best, point)) {
    const int interval =
        std::max(180, settings.triggerIntervalMs) + randomInt(-40, 70);
    m_nextAttackAtMs = now + interval;
  }
}

void AimAssist::updateTrigger(void *player, const Settings &settings,
                              const ViewAngles &view, const Vec3 &eye,
                              float dt) {
  (void)dt;
  if (!player || !settings.triggerBot) {
    m_triggerActor.store(nullptr, std::memory_order_release);
    m_triggerDistance.store(0.0f, std::memory_order_relaxed);
    m_onTarget.store(false, std::memory_order_release);
    return;
  }

  const Vec3 dir = directionFromAngles(view);
  const float reach = clampf(settings.triggerRange, 2.0f, 5.0f);
  const float tolerance = clampf(settings.triggerBoxTolerance, 0.0f, 0.25f);

  void *bestActor = nullptr;
  float bestDistance = reach + tolerance;

  int tested = 0;
  int skippedBounds = 0;
  float minAngle = 1e9f;

  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;
    // Whichever hitbox the crosshair is actually on gets hit.

    const bool playerActor = m_bridge.isPlayer(actor);
    if (playerActor ? !settings.targetPlayers : !settings.targetMobs) continue;
    if (settings.ignoreInvisible && isInvisible(actor)) continue;
    if (settings.teamCheck && isSameTeam(player, actor)) continue;
    if (playerActor && Esp::instance().isFriend(actor)) continue;
    if (playerActor && settings.antiBot &&
        AntiBot::instance().isBot(actor)) {
      continue;
    }

    Aabb bounds{};
    if (!m_bridge.getActorBounds(actor, bounds)) {
      ++skippedBounds;
      continue;
    }

    {
      const Vec3 center{(bounds.min.x + bounds.max.x) * 0.5f,
                        (bounds.min.y + bounds.max.y) * 0.5f,
                        (bounds.min.z + bounds.max.z) * 0.5f};
      const float dx = center.x - eye.x;
      const float dy = center.y - eye.y;
      const float dz = center.z - eye.z;
      const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (len > 1e-4f) {
        const float dot =
            clampf((dir.x * dx + dir.y * dy + dir.z * dz) / len, -1.0f, 1.0f);
        minAngle = std::min(minAngle, std::acos(dot) * kRadToDeg);
      }
    }
    ++tested;

    // Normal trigger bot: only the real hitbox plus a small interpolation
    // tolerance is attackable. No angular expansion, no silent aim.
    const float tolY = std::max(tolerance, 0.06f) + 0.04f;
    Aabb padded = bounds;
    padded.min.x -= tolerance;
    padded.min.z -= tolerance;
    padded.max.x += tolerance;
    padded.max.z += tolerance;
    padded.min.y -= tolY;
    padded.max.y += tolY;

    float hit = 0.0f;
    if (!rayIntersectsAabb(eye, dir, padded, bestDistance, hit)) continue;
    if (hit >= bestDistance) continue;
    bestDistance = hit;
    bestActor = actor;
  }

  const int64_t now = monotonicMs();

  static uint64_t s_triggerLog = 0;
  if ((++s_triggerLog % 180) == 1) {
    const bool onGround = player && gameecs::hasOnGround(player);
    const long long fallMs = m_fallingSinceMs != 0
                                 ? static_cast<long long>(now - m_fallingSinceMs)
                                 : -1;
    const long long nextIn = m_nextAttackAtMs > now
                                 ? static_cast<long long>(m_nextAttackAtMs - now)
                                 : 0;
    const long long sinceMs = m_triggerSinceMs != 0
                                  ? static_cast<long long>(now - m_triggerSinceMs)
                                  : -1;
    aimbot::diagLog(
        kTag,
        "trigger actors=%zu tested=%d noBounds=%d minAng=%.1f "
        "best=%p bestDist=%.2f reach=%.2f vv=%.2f ground=%d "
        "fallMs=%lld since=%lld delay=%d nextIn=%lld onlyCrit=%d fast=%d",
        m_actors.size(), tested, skippedBounds,
        static_cast<double>(minAngle),
        bestActor, static_cast<double>(bestDistance),
        static_cast<double>(reach), static_cast<double>(m_vertVel),
        onGround ? 1 : 0, fallMs, sinceMs, m_triggerDelayForTargetMs,
        nextIn, settings.onlyCrit ? 1 : 0, settings.fastAim ? 1 : 0);
  }

  m_triggerActor.store(bestActor, std::memory_order_release);
  m_triggerDistance.store(bestActor ? bestDistance : 0.0f,
                          std::memory_order_relaxed);
  m_onTarget.store(bestActor != nullptr, std::memory_order_release);

  if (!bestActor) {
    // One dropped frame (target bobbing between 20 Hz positions, or the
    // crosshair clipping the box edge for a single frame) must not restart
    // the reaction delay. Keep the acquisition timer for a short grace.
    if (m_triggerSinceActor != nullptr &&
        now - m_triggerLastSeenMs > 120) {
      m_triggerSinceActor = nullptr;
      m_triggerSinceMs = 0;
    }
    return;
  }
  m_triggerLastSeenMs = now;

  // Атака выполняется прямо здесь, на игровом потоке: тот же путь, что у
  // внутренних читов (Killaura/Aura) — GameMode::attack, без инъекции
  // касаний и без участия UI-потока.
  if (settings.onlyCrit) {
    // Keep the server-side sprint state clear for the whole fall, not just
    // on the attack frame: the attack packet is processed before the next
    // auth input, so the previous ticks must already be non-sprinting.
    // This starts at the beginning of the descent, before the crit settle
    // window, so the server has a non-sprint tick by the time we attack.
    const float critGate = -std::max(0.10f, settings.critMinFallSpeed);
    const bool descending =
        player && m_vertVel <= critGate;
    if (descending) {
      if (m_fallingSinceMs == 0) m_fallingSinceMs = now;
      combatstate::suppressSprint(150);
      gameecs::setMoveSprinting(player, false);
      gameecs::setSprintingFlag(player, false);
    }
  }

  if (m_triggerSinceActor != bestActor) {
    m_triggerSinceActor = bestActor;
    m_triggerSinceMs = now;
    // Реакция на новую цель — со случайным разбросом, а не константа.
    m_triggerDelayForTargetMs = settings.triggerDelayMs;
    const int jitter = std::max(0, settings.triggerDelayJitterMs);
    if (jitter > 0) {
      m_triggerDelayForTargetMs += randomInt(0, jitter);
    }
    if (settings.killaura) {
      m_triggerDelayForTargetMs =
          std::max(m_triggerDelayForTargetMs,
                   static_cast<int>(settings.killauraReactionMs));
    }
    m_triggerDelayForTargetMs =
        std::max(0, std::min(400, m_triggerDelayForTargetMs));
  }
  // В режиме only crit окно падения короткое: не тратим его на полную
  // «человеческую» задержку и случайные пропуски.
  const int64_t effectiveDelay =
      settings.onlyCrit ? std::min(m_triggerDelayForTargetMs, 40)
                        : static_cast<int64_t>(m_triggerDelayForTargetMs);
  if (now - m_triggerSinceMs < effectiveDelay) return;
  if (m_nextAttackAtMs != 0 && now < m_nextAttackAtMs) return;

  // Не бьём в кадре быстрого довода. Polar/ML смотрят не только на угол
  // прицела, но и на угловую скорость в момент удара: удар на 20-30°/тик
  // выглядит как killaura даже при идеальном попадании.
  if (settings.antiCheatMode) {
    // A 14 deg/tick cap blocked hits during the aim-assist approach itself
    // (the camera legitimately turns 20-25 deg/tick on a fast flick).
    const float attackSpeedLimit =
        std::max(30.0f, settings.sustainedTickDeltaDeg);
    if (m_prevTickDeltaDeg > attackSpeedLimit) {
      m_nextAttackAtMs = now + 45;
      return;
    }
  }

  // Не бьём сразу после смены цели (короткий анти-дребезг, не 120 мс) и
  // иногда «промахиваемся» (не жмём).
  if (settings.humanize && !settings.onlyCrit) {
    if (m_lastSwitchMs != 0 && now - m_lastSwitchMs < 35) return;
    if (settings.attackMissChance > 0.0f &&
        randomFloat(0.0f, 1.0f) < settings.attackMissChance) {
      m_nextAttackAtMs = now + randomInt(80, 220);
      return;
    }
  }

  Vec3 hitPoint{};
  hitPoint.x = eye.x + dir.x * bestDistance;
  hitPoint.y = eye.y + dir.y * bestDistance;
  hitPoint.z = eye.z + dir.z * bestDistance;
  if (settings.randomAimPoint) {
    hitPoint = randomHitPoint(bestActor, hitPoint);
  }

  // Pull the attack point a few centimetres inside the hitbox. A ray that
  // only clips the side of the client AABB otherwise lands outside the
  // server's slightly different (interpolated) box and the hit is dropped.
  {
    Aabb box{};
    if (m_bridge.getActorBounds(bestActor, box)) {
      const float mx = std::min(0.09f, (box.max.x - box.min.x) * 0.30f);
      const float my = std::min(0.09f, (box.max.y - box.min.y) * 0.20f);
      const float mz = std::min(0.09f, (box.max.z - box.min.z) * 0.30f);
      hitPoint.x = clampf(hitPoint.x, box.min.x + mx, box.max.x - mx);
      hitPoint.y = clampf(hitPoint.y, box.min.y + my, box.max.y - my);
      hitPoint.z = clampf(hitPoint.z, box.min.z + mz, box.max.z - mz);
    }
  }

  // Never send an attack through a wall: the server rejects it (and
  // anti-cheat treats it as a reach/through-wall hit). The ray test only
  // knows actor boxes, not blocks.
  if (settings.visibleCheck &&
      !m_bridge.hasLineOfSight(player, eye, hitPoint)) {
    m_nextAttackAtMs = now + 50;
    return;
  }

  // Главная сетевая проверка: бьём только если ротация, которую сервер
  // уже получил на последнем тике 20 Гц, тоже смотрит на точку удара.
  // Иначе Java-античит (Polar/GhostAC) сравнивает удар со старой
  // ротацией и видит «удар смотря в сторону».
  if (settings.serverViewGuard && m_haveTickSample) {
    const int64_t netAgeMs = now - m_lastNetTickMs;
    if (netAgeMs >= 0 && netAgeMs <= 150) {
      ViewAngles netView{};
      netView.pitch = m_networkPitch.load(std::memory_order_relaxed);
      netView.yaw = m_networkYaw.load(std::memory_order_relaxed);
      const Vec3 netDir = directionFromAngles(netView);
      const float hx = hitPoint.x - eye.x;
      const float hy = hitPoint.y - eye.y;
      const float hz = hitPoint.z - eye.z;
      const float hlen = std::sqrt(hx * hx + hy * hy + hz * hz);
      float netAngle = 180.0f;
      if (hlen > 1e-4f) {
        const float dot = clampf(
            (netDir.x * hx + netDir.y * hy + netDir.z * hz) / hlen, -1.0f,
            1.0f);
        netAngle = std::acos(dot) * kRadToDeg;
      }
      float guardTolerance = settings.serverViewToleranceDeg;
      if (settings.onlyCrit) {
        // Vertical movement widens the error between the last 20 Hz auth
        // rotation and the hit point; crits are impossible without it.
        guardTolerance = std::max(guardTolerance, 18.0f);
      } else {
        // The old 4 deg cap blocked most attacks while the camera tracked a
        // strafing player: the network sample is up to 50 ms old. 8 deg is
        // still tight enough that the hit looks aligned server-side.
        guardTolerance = std::max(guardTolerance, 8.0f);
      }
      if (netAngle > guardTolerance) {
        m_serverViewSinceMs = 0;
        m_nextAttackAtMs = now + 30;
        static uint64_t s_guardLog = 0;
        if ((++s_guardLog % 200) == 1) {
          aimbot::diagLog(
              kTag,
              "trigger guard: server view off by %.2f deg (tol %.1f), "
              "age=%lldms",
              static_cast<double>(netAngle),
              static_cast<double>(guardTolerance),
              static_cast<long long>(netAgeMs));
        }
        return;
      }
    }
  }

  // Only crit: удар только в падении после прыжка. На земле/в подъёме
  // атака не отправляется, поэтому каждый удар идёт критом.
  if (settings.onlyCrit && !isFallingForCrit(settings, now)) {
    return;
  }

  // The server validates the hit against the rotation/state in the latest
  // PlayerAuthInput. Fire shortly AFTER a 20 Hz tick (0-45 ms) so that the
  // sample is fresh: this is what makes the server-view guard pass during
  // real PvP flicks instead of dropping the hit.
  if (m_haveTickSample) {
    const int64_t tickAge = now - m_lastNetTickMs;
    if (tickAge < 0 || tickAge > 45) {
      m_nextAttackAtMs = now + 5;
      return;
    }
  }

  {
    static uint64_t s_fireLog = 0;
    const uint64_t n = ++s_fireLog;
    if (n <= 5 || (n % 50) == 0) {
      aimbot::diagLog(kTag,
                      "trigger fire #%llu actor=%p dist=%.2f gm=%p "
                      "camAng=%.1f",
                      static_cast<unsigned long long>(n), bestActor,
                      static_cast<double>(bestDistance),
                      m_bridge.getGameMode(),
                      static_cast<double>(minAngle));
    }
  }
  // Only criticals need the server-side sprint state cleared. Doing it on
  // every attack silently cancelled the player's sprint and made running
  // fights feel like the trigger stopped working; normal sprint hits are
  // valid vanilla behavior.
  if (settings.onlyCrit) {
    combatstate::suppressSprint(120);
    if (player) {
      gameecs::setMoveSprinting(player, false);
      gameecs::setSprintingFlag(player, false);
    }
  }

  if (m_bridge.attackActor(bestActor, hitPoint)) {
    // Target ESP появляется только на факте удара TriggerBot.
    Esp::instance().markTargetEspHit(bestActor);
    // Анимация взмаха применяется только к удару TriggerBot.
    m_bridge.armSwing();
    // Bedrock gives the victim a 10-tick (500 ms) damage-immunity window.
    // Attacking every 220 ms (the old default) meant every second packet was
    // discarded and crit windows were wasted. Never schedule inside the
    // window; a tiny positive jitter avoids a perfectly periodic packet.
    int64_t next =
        now + std::max<int64_t>(settings.triggerIntervalMs, 500);
    // Two-tier interval jitter: mostly a tight 0-28 ms, sometimes a longer
    // 30-70 ms gap. A constant cadence is one of the easiest ML features.
    const int maxJitter =
        std::min(70, std::max(30, settings.attackIntervalJitterMs));
    const int jitter = randomFloat(0.0f, 1.0f) < 0.75f
                           ? randomInt(0, 28)
                           : randomInt(30, maxJitter);
    next += jitter;
    m_nextAttackAtMs = std::max(now + 80, next);
  } else {
    // GameMode ещё не готов (меню/загрузка) — не долбим каждый кадр.
    m_nextAttackAtMs = now + 100;
  }
}

Angles AimAssist::calculateAngles(void *entity) const {
  Angles angles{};
  void *player = m_bridge.getLocalPlayer();
  if (!player || !entity) return angles;

  Vec3 eye{};
  if (!m_bridge.getEyePosition(player, eye)) return angles;

  const Settings settings = getSettings();
  const Vec3 point = getTargetPoint(entity, settings.targetBone, settings.triggerBot);
  const float dx = point.x - eye.x;
  const float dy = point.y - eye.y;
  const float dz = point.z - eye.z;
  const float horizontal = std::max(0.0001f, std::sqrt(dx * dx + dz * dz));

  angles.yaw = normalizeYaw(std::atan2(-dx, dz) * kRadToDeg);
  angles.pitch =
      clampf(-std::atan2(dy, horizontal) * kRadToDeg, -89.9f, 89.9f);
  return angles;
}

Angles AimAssist::calculateAnglesToPoint(const Vec3 &point) const {
  Angles angles{};
  const float dx = point.x - m_eye.x;
  const float dy = point.y - m_eye.y;
  const float dz = point.z - m_eye.z;
  // Overlapping players can make the aim point coincide with the eye. Without
  // this guard atan2(0,0) returned yaw=0/pitch=0 and the camera snapped.
  if (dx * dx + dy * dy + dz * dz < 1.0e-6f) {
    angles.pitch = m_localPitch.load(std::memory_order_relaxed);
    angles.yaw = m_localYaw.load(std::memory_order_relaxed);
    return angles;
  }
  const float horizontal = std::max(0.0001f, std::sqrt(dx * dx + dz * dz));

  angles.yaw = normalizeYaw(std::atan2(-dx, dz) * kRadToDeg);
  angles.pitch =
      clampf(-std::atan2(dy, horizontal) * kRadToDeg, -89.9f, 89.9f);
  return angles;
}

void AimAssist::stabilizeFastAimAngles(float dt) {
  if (!m_haveStableAim || m_stableAimActor != m_target.actor) {
    m_haveStableAim = true;
    m_stableAimActor = m_target.actor;
    m_stableAimYaw = m_target.yaw;
    m_stableAimPitch = m_target.pitch;
    return;
  }

  const float errYaw = shortestDelta(m_stableAimYaw, m_target.yaw);
  const float errPitch = m_target.pitch - m_stableAimPitch;
  const float err = std::sqrt(errYaw * errYaw + errPitch * errPitch);

  if (err > 6.0f) {
    m_stableAimYaw = m_target.yaw;
    m_stableAimPitch = m_target.pitch;
    return;
  }

  // Малые дрожания сглаживаем, заметное движение цели пропускаем без
  // задержки: alpha растёт до 1 уже на 0.6°, поэтому скорость флика не
  // меняется. Базовый коэффициент снижен с 55 до 20 рад/с: точка цели
  // приходит из интерполированного 20 Гц хитбокса, и её шаги были видны
  // как остаточный микро-тремор.
  const float baseAlpha =
      1.0f - std::exp(-clampf(dt, 0.0f, 0.05f) * 20.0f);
  const float motion = clampf(err / 0.6f, 0.0f, 1.0f);
  const float alpha = baseAlpha + (1.0f - baseAlpha) * motion;
  m_stableAimYaw = normalizeYaw(m_stableAimYaw + errYaw * alpha);
  m_stableAimPitch =
      clampf(m_stableAimPitch + errPitch * alpha, -89.9f, 89.9f);
  m_target.yaw = m_stableAimYaw;
  m_target.pitch = m_stableAimPitch;
}

void AimAssist::updateFastAimStealth(const Settings &, float) {
  m_noiseYaw = 0.0f;
  m_noisePitch = 0.0f;
  m_restYaw = 0.0f;
  m_restPitch = 0.0f;
  m_restTargetYaw = 0.0f;
  m_restTargetPitch = 0.0f;
}

// ============================================================
// Плавный довод
// ============================================================
void AimAssist::applySmoothRotation(float targetYaw, float targetPitch,
                                    float dt) {
  const Settings settings = getSettings();
  applySmoothRotationInternal(targetYaw, targetPitch, dt, settings);
}

void AimAssist::applySmoothRotationInternal(float targetYaw, float targetPitch,
                                            float dt,
                                            const Settings &settings) {
  dt = clampf(dt, 0.0f, kMaxDt);
  if (dt <= 0.0f) {
    return;
  }

  // Один шаг на кадр: applyTurnDelta нельзя вызывать несколько раз за кадр,
  // иначе камера получает несколько мелких рывков и дрожит.
  const float h = dt;

  // ------------------------------------------------------------
  // Fast aim.
  //
  // antiCheatMode=false: прежняя чистая экспонента (быстро, но палевно).
  // antiCheatMode=true: быстрый довод внутри человеческого конверта —
  // случайный профиль на каждый бой, разгон с ограничением ускорения,
  // OU-шум/смещение точки, бюджет поворота на серверный тик и пауза
  // реакции. Это не «гарантия обхода» ML: серверные модели ловят саму
  // статистику идеального трекинга, поэтому профиль рандомизирован.
  // ------------------------------------------------------------
  if (settings.fastAim) {
    const float curYaw = m_localYaw.load(std::memory_order_relaxed);
    const float curPitch = m_localPitch.load(std::memory_order_relaxed);
    const float fastDt = clampf(dt, 0.001f, 0.05f);
    const float strengthScale =
        clampf(settings.strength / 100.0f, 0.15f, 1.0f);
    const float timeMs =
        clampf(settings.fastAimTimeMs / strengthScale * m_fastTimeScale,
               12.0f, 200.0f);
    const float alpha = 1.0f - std::exp(-fastDt * 1000.0f / timeMs);

    float errYaw = shortestDelta(curYaw, targetYaw);
    float errPitch = targetPitch - curPitch;
    const float errMag =
        std::sqrt(errYaw * errYaw + errPitch * errPitch);

    const float maxSpeed =
        clampf(settings.fastAimMaxSpeed * strengthScale * m_fastSpeedScale *
                   m_motorScale,
               300.0f, 4000.0f);

    // Hard ceiling on how much the view may move in ONE rendered frame.
    // Exponential smoothing alone still produces a large first step when the
    // game runs at 15-30 FPS, which read as mini teleports. A degree-based
    // cap keeps motion continuous at any frame rate; the ceiling is higher
    // for large corrections so a PvP flick still snaps in ~100-200 ms.
    // PC-cheat feel: a fast flick that still interpolates every frame. The
    // cap grows with dt so the angular speed stays similar at any FPS, but it
    // never exceeds ~10-12 deg/frame - the range where a 30-60 FPS camera
    // reads as motion, not as a teleport. Large errors get a small bonus and
    // the exponential tail settles the last degrees smoothly.
    const float frameCap =
        clampf(300.0f * fastDt, 6.0f, 13.0f) +
        3.5f * clampf((errMag - 10.0f) / 50.0f, 0.0f, 1.0f);

    float stepYaw;
    float stepPitch;
    if (settings.legit) {
      // Smooth approach instead of the old rate-limited variant that moved
      // stepLen = errMag for every error below maxSpeed * dt. That consumed
      // the whole remaining error in a single frame, which read as a camera
      // teleport whenever the crosshair was outside the hitbox.
      //
      // ~50 ms time constant: the exponential tail settles quickly, while
      // the per-frame cap above keeps the initial flick continuous.
      const float tauMs =
          clampf(std::max(settings.fastAimTimeMs * 1.5f, 45.0f),
                 40.0f, 120.0f);
      const float smoothAlpha =
          1.0f - std::exp(-fastDt * 1000.0f / tauMs);
      const float distanceEase =
          0.70f + 0.30f * clampf(errMag / 6.0f, 0.0f, 1.0f);
      float stepLen = errMag * smoothAlpha * distanceEase;

      // Feed-forward of the target's own angular velocity so strafing targets
      // are not trailed. It must not use a sample from a different actor:
      // a target switch would inject a huge fake velocity and snap the view.
      if (m_haveTargetSample) {
        const float targetRate =
            (std::fabs(shortestDelta(m_lastTargetYaw, targetYaw)) +
             std::fabs(targetPitch - m_lastTargetPitch)) /
            std::max(fastDt, 1e-3f);
        m_catchupRate += (targetRate - m_catchupRate) *
                         (1.0f - std::exp(-fastDt / 0.12f));
        stepLen += clampf(m_catchupRate, 0.0f, 600.0f) * fastDt * 0.35f;
      } else {
        m_catchupRate = 0.0f;
      }
      stepLen = std::min(stepLen, maxSpeed * fastDt);
      stepLen = std::min(stepLen, frameCap);
      // Jerk limit: the per-frame step ramps toward its target instead of
      // starting/stopping instantly. This is what makes the flick read as a
      // continuous mouse motion rather than a series of snap frames.
      const float velAlpha =
          1.0f - std::exp(-fastDt / 0.020f);
      m_smoothStep += (stepLen - m_smoothStep) * velAlpha;
      m_smoothStep = std::min(m_smoothStep, frameCap);
      stepLen = std::min(m_smoothStep, errMag);
      // Deadzone: do not chase the last tenth of a degree. Those constant
      // tiny corrections are what still read as crosshair tremor.
      if (errMag < 0.12f) {
        m_smoothStep = 0.0f;
        stepLen = 0.0f;
      }
      stepYaw = errMag > 1e-6f ? errYaw * (stepLen / errMag) : errYaw;
      stepPitch = errMag > 1e-6f ? errPitch * (stepLen / errMag) : errPitch;
    } else {
      stepYaw = errYaw * alpha;
      stepPitch = errPitch * alpha;
      const float maxStep = std::min(maxSpeed * fastDt, frameCap);
      const float stepMag =
          std::sqrt(stepYaw * stepYaw + stepPitch * stepPitch);
      if (stepMag > maxStep && stepMag > 1e-6f) {
        const float scale = maxStep / stepMag;
        stepYaw *= scale;
        stepPitch *= scale;
      }
    }
    m_lastTargetYaw = targetYaw;
    m_lastTargetPitch = targetPitch;
    m_haveTargetSample = true;
    if (errMag < 0.01f) {
      stepYaw = errYaw;
      stepPitch = errPitch;
    }

    // Ограничение именно на серверный тик 20 Гц. Обычное сопровождение
    // сюда не упирается, а длинные снапы Polar/ML видят как rotation-check.
    if (settings.antiCheatMode) {
      const float tickCap =
          clampf(settings.maxTickDeltaDeg, 12.0f, 60.0f);
      float ny = m_tickAccumYaw + stepYaw;
      float np = m_tickAccumPitch + stepPitch;
      const float mag = std::sqrt(ny * ny + np * np);
      if (mag > tickCap && mag > 1e-5f) {
        const float k = tickCap / mag;
        ny *= k;
        np *= k;
        stepYaw = ny - m_tickAccumYaw;
        stepPitch = np - m_tickAccumPitch;
      }
      m_tickAccumYaw += stepYaw;
      m_tickAccumPitch += stepPitch;
    }

    m_localYaw.store(normalizeYaw(curYaw + stepYaw),
                     std::memory_order_relaxed);
    m_localPitch.store(clampf(curPitch + stepPitch, -89.9f, 89.9f),
                       std::memory_order_relaxed);
    m_lastDelta = errMag;
    m_lastFactor = 1.0f;
    m_lastDt = fastDt;
    m_deadzoneHit = false;

    void *rawPlayer = m_bridge.getLocalPlayer();
    if (rawPlayer && (std::fabs(stepYaw) > 0.00005f ||
                      std::fabs(stepPitch) > 0.00005f)) {
      m_bridge.applyTurnDelta(rawPlayer, -stepPitch, stepYaw);
      m_fastAimTurnApplied = true;
    }
    return;
  }

  const float strengthScale = clampf(settings.strength / 100.0f, 0.15f, 1.0f);
  const float curYaw = m_localYaw.load(std::memory_order_relaxed);
  const float curPitch = m_localPitch.load(std::memory_order_relaxed);
  float errYaw = shortestDelta(curYaw, targetYaw);
  float errPitch = targetPitch - curPitch;
  const float errMag =
      std::sqrt(errYaw * errYaw + errPitch * errPitch);

  const float timeMs = clampf(
      1800.0f / std::max(1.0f, settings.smoothingSpeed) / strengthScale,
      10.0f, 200.0f);
  const float alpha = 1.0f - std::exp(-h * 1000.0f / timeMs);
  float stepYaw = errYaw * alpha;
  float stepPitch = errPitch * alpha;

  // Same per-frame degree ceiling as the fast path: the old smoothing could
  // move 40%+ of the remaining error in a single frame at low FPS.
  const float frameCap =
      clampf(200.0f * h, 4.0f, 9.0f) +
      2.5f * clampf((errMag - 10.0f) / 50.0f, 0.0f, 1.0f);
  const float maxStep = std::min(
      settings.maxDegPerSecond * (0.5f + 0.5f * strengthScale) * m_motorScale * h,
      frameCap);
  const float stepMag =
      std::sqrt(stepYaw * stepYaw + stepPitch * stepPitch);
  if (stepMag > maxStep && stepMag > 1e-6f) {
    const float scale = maxStep / stepMag;
    stepYaw *= scale;
    stepPitch *= scale;
  }
  if (errMag < 0.01f) {
    stepYaw = errYaw;
    stepPitch = errPitch;
  }

  m_localYaw.store(normalizeYaw(curYaw + stepYaw),
                   std::memory_order_relaxed);
  m_localPitch.store(clampf(curPitch + stepPitch, -89.9f, 89.9f),
                     std::memory_order_relaxed);
  m_velYaw = stepYaw / std::max(h, 0.001f);
  m_velPitch = stepPitch / std::max(h, 0.001f);
  m_lastDelta = errMag;
  m_lastFactor = clampf(
      std::sqrt(stepYaw * stepYaw + stepPitch * stepPitch) /
          std::max(0.001f, settings.maxDegPerSecond * h),
      0.0f, 1.0f);
  m_deadzoneHit = false;
}

// ============================================================
// Человеческий моторный профиль
// ============================================================
float AimAssist::randomFloat(float lo, float hi) {
  if (hi <= lo) return lo;
  std::uniform_real_distribution<float> dist(lo, hi);
  return dist(m_rng);
}

int AimAssist::randomInt(int lo, int hi) {
  if (hi <= lo) return lo;
  std::uniform_int_distribution<int> dist(lo, hi);
  return dist(m_rng);
}

void AimAssist::resetHumanProfile() {
  m_noiseYaw = 0.0f;
  m_noisePitch = 0.0f;
  m_restYaw = 0.0f;
  m_restPitch = 0.0f;
  m_restTargetYaw = 0.0f;
  m_restTargetPitch = 0.0f;
  m_overYaw = 0.0f;
  m_overPitch = 0.0f;
  m_motorScale = 1.0f;
  m_nextMotorMs = 0;
  m_nextRestMs = 0;
  m_targetLockMs = 0;
  // A new engagement must not inherit the previous target's angular velocity.
  m_catchupRate = 0.0f;
  m_haveTargetSample = false;
  m_smoothStep = 0.0f;
  m_reachFrames = 0;
}

void AimAssist::updateHumanProfile(const Settings &settings, float, int64_t now) {
  // Only the smooth motor-speed variation stays; all per-frame noise, rest
  // offset and overshoot were removed so the aim never trembles.
  if (now >= m_nextMotorMs) {
    const float var = clampf(settings.motorVariation, 0.0f, 0.45f);
    m_motorScale = randomFloat(1.0f - var, 1.0f + var);
    m_nextMotorMs = now + randomInt(180, 480);
  }
  m_noiseYaw = 0.0f;
  m_noisePitch = 0.0f;
  m_restYaw = 0.0f;
  m_restPitch = 0.0f;
  m_overYaw = 0.0f;
  m_overPitch = 0.0f;
}

void AimAssist::updateFatigue(const Settings &settings, int64_t now) {
  if (!settings.humanize) {
    m_fatigueScale = 1.0f;
    return;
  }
  if (m_nextFatigueMs == 0) {
    m_nextFatigueMs = now + randomInt(2500, 6000);
    m_fatigueUntilMs = 0;
  }
  if (now >= m_nextFatigueMs && now >= m_fatigueUntilMs) {
    m_fatigueScale = randomFloat(0.78f, 0.95f);
    m_fatigueUntilMs = now + randomInt(300, 900);
    m_nextFatigueMs = now + randomInt(2500, 6000);
  } else if (now >= m_fatigueUntilMs) {
    m_fatigueScale += (1.0f - m_fatigueScale) * 0.08f;
    if (m_fatigueScale > 0.999f) m_fatigueScale = 1.0f;
  }
}

// Бюджет поворота на один тик сервера (50 мс). Ограничивает то, что
// реально уйдёт в PlayerAuthInput: устойчивую скорость, прирост скорости
// между тиками и суммарную дельту. Короткий флик разрешён отдельно.
void AimAssist::applyTickBudget(float &stepYaw, float &stepPitch,
                                const Settings &settings, float) {
  const float cap = (m_flickTicksLeft > 0)
                        ? std::max(1.0f, settings.maxTickDeltaDeg)
                        : std::min(std::max(1.0f, settings.maxTickDeltaDeg),
                                   std::max(1.0f, settings.sustainedTickDeltaDeg));
  const float growth = std::max(0.0f, settings.maxTickAccelDeg);
  const float allowed = std::min(cap, std::fabs(m_prevTickDeltaDeg) + growth);

  bool clampedYaw = false;
  bool clampedPitch = false;
  auto limitAxis = [&](float &step, float accum, bool &clamped) {
    const float target = accum + step;
    if (std::fabs(target) > allowed) {
      const float limited = std::copysign(allowed, target);
      const float next = limited - accum;
      // Не разворачиваем камеру бюджетом резко в противоположную сторону,
      // но и не обнуляем шаг (обнуление даёт дрожание на высокой скорости).
      if (next * step < 0.0f) {
        step = next * 0.35f;
      } else {
        step = next;
      }
      clamped = true;
    }
  };
  limitAxis(stepYaw, m_tickAccumYaw, clampedYaw);
  limitAxis(stepPitch, m_tickAccumPitch, clampedPitch);

  // Суммарная дельта по обеим осям тоже не должна превышать бюджет.
  const float ny = m_tickAccumYaw + stepYaw;
  const float np = m_tickAccumPitch + stepPitch;
  const float mag = std::sqrt(ny * ny + np * np);
  if (mag > allowed && mag > 1e-5f) {
    const float k = allowed / mag;
    stepYaw = ny * k - m_tickAccumYaw;
    stepPitch = np * k - m_tickAccumPitch;
    clampedYaw = true;
    clampedPitch = true;
  }

  if (clampedYaw) m_velYaw *= 0.5f;
  if (clampedPitch) m_velPitch *= 0.5f;

  m_tickAccumYaw += stepYaw;
  m_tickAccumPitch += stepPitch;
}

void AimAssist::updateVerticalMotion(float feetY, int64_t now) {
  if (!m_haveEyeSample) {
    m_lastEyeY = feetY;
    m_lastEyeMs = now;
    m_haveEyeSample = true;
    m_vertVel = 0.0f;
    return;
  }
  const float dt = clampf(
      static_cast<float>(now - m_lastEyeMs) / 1000.0f, 0.001f, 0.10f);
  const float instant = (feetY - m_lastEyeY) / dt;
  // Респавн/телепорт/смена измерения — сбрасываем, иначе получим ложное
  // «падение» с огромной скоростью.
  if (std::fabs(instant) > 40.0f) {
    m_vertVel = 0.0f;
    m_fallingSinceMs = 0;
  } else {
    const float alpha = clampf(dt / 0.07f, 0.0f, 1.0f);
    m_vertVel += (instant - m_vertVel) * alpha;
  }
  m_lastEyeY = feetY;
  m_lastEyeMs = now;
}

// Крит в Bedrock засчитывается в падении (не на земле, вертикальная
// скорость отрицательная). Ждём несколько миллисекунд непрерывного
// падения, чтобы не поймать шум на вершине прыжка.
bool AimAssist::isFallingForCrit(const Settings &settings, int64_t now) {
  void *player = m_bridge.getLocalPlayer();
  if (!player) return false;
  // Vanilla crit: airborne and descending. Holding the jump button while
  // already airborne does NOT prevent a crit, so never disqualify on the
  // jump input alone — doing so made onlyCrit silently stop firing for
  // players who bunny-hop with the button held.
  const float gate = -std::max(0.10f, settings.critMinFallSpeed);
  // The on-ground component can lag one tick on some builds; do not let a
  // stale `true` suppress crits forever. Only a grounded AND non-descending
  // player is disqualified.
  if (gameecs::hasOnGround(player) && m_vertVel > gate) {
    m_fallingSinceMs = 0;
    return false;
  }
  // Clearly rising means a new jump (or jump boost): restart the timer.
  // Small wobble around the apex must NOT restart it, otherwise the settle
  // window keeps resetting and the attack lands late in the fall.
  if (m_vertVel > 0.6f) {
    m_fallingSinceMs = 0;
    return false;
  }
  if (m_vertVel <= gate && m_fallingSinceMs == 0) {
    m_fallingSinceMs = now;
  }
  if (m_fallingSinceMs == 0) return false;
  // Wait at least one 20 Hz tick after the descent starts so the server has
  // already received a PlayerAuthInput with "airborne, falling, not
  // sprinting". Then only attack while vv is still negative: at the apex
  // noise the server may see the player as rising and refuse the crit.
  const int64_t settleMs =
      static_cast<int64_t>(std::max(0, settings.critSettleMs));
  // Require a clear descent (not just "vaguely negative"): at the apex the
  // server may still treat the player as rising and refuse the crit.
  return m_vertVel <= gate && now - m_fallingSinceMs >= settleMs;
}

// ============================================================
// Вспомогательное
// ============================================================
Vec3 AimAssist::getTargetPoint(void *actor, TargetBone bone,
                               bool preferCenter) const {
  (void)preferCenter;
  Aabb bounds{};
  if (!m_bridge.getActorBounds(actor, bounds)) {
    Vec3 position{};
    m_bridge.getPosition(actor, position);
    return position;
  }
  const float height = std::max(0.1f, bounds.max.y - bounds.min.y);
  // One fixed body-part point per frame. The old "already on the hitbox ->
  // return a point on the live ray" shortcut made the goal jump between the
  // ray point (error 0) and the body point (10-20 deg) whenever the target
  // crossed the hitbox edge, which read as a teleporting crosshair. A fixed
  // bone gives the smoothing a continuous goal, like PC aimbots.
  float pointY = (bounds.min.y + bounds.max.y) * 0.5f;
  switch (bone) {
    case TargetBone::Head:
      pointY = bounds.max.y - std::min(0.22f, height * 0.12f);
      break;
    case TargetBone::UpperChest:
      // Just below eye level.
      pointY = bounds.min.y + height * 0.84f;
      break;
    case TargetBone::Chest:
      // Slightly above the shoulders: between shoulder and eye level.
      pointY = bounds.min.y + height * 0.80f;
      break;
    case TargetBone::LowerChest:
      pointY = bounds.min.y + height * 0.62f;
      break;
    case TargetBone::Legs:
      pointY = bounds.min.y + height * 0.26f;
      break;
  }
  return {(bounds.min.x + bounds.max.x) * 0.5f, pointY,
          (bounds.min.z + bounds.max.z) * 0.5f};
}

Vec3 AimAssist::jitteredAimPoint(void *actor, const Vec3 &base,
                                 TargetBone bone) {
  const int64_t now = nowMs();
  if (actor != m_aimPointActor || now >= m_nextAimPointMs) {
    m_aimPointActor = actor;
    const bool head = bone == TargetBone::Head;
    const float horizontal = head ? 0.0016f : 0.0026f;
    const float vertical = head ? 0.0012f : 0.0022f;
    m_aimPointOffset.x = randomFloat(-horizontal, horizontal);
    m_aimPointOffset.y = randomFloat(-vertical, vertical);
    m_aimPointOffset.z = randomFloat(-horizontal, horizontal);
    m_nextAimPointMs = now + randomInt(2500, 5000);
  }

  Aabb bounds{};
  if (!m_bridge.getActorBounds(actor, bounds)) return base;

  Vec3 point{base.x + m_aimPointOffset.x,
             base.y + m_aimPointOffset.y,
             base.z + m_aimPointOffset.z};
  const float marginX = std::min(0.08f, (bounds.max.x - bounds.min.x) * 0.2f);
  const float marginY = std::min(0.08f, (bounds.max.y - bounds.min.y) * 0.2f);
  const float marginZ = std::min(0.08f, (bounds.max.z - bounds.min.z) * 0.2f);
  point.x = clampf(point.x, bounds.min.x + marginX, bounds.max.x - marginX);
  point.y = clampf(point.y, bounds.min.y + marginY, bounds.max.y - marginY);
  point.z = clampf(point.z, bounds.min.z + marginZ, bounds.max.z - marginZ);
  return point;
}

Vec3 AimAssist::randomHitPoint(void *actor, const Vec3 &hitPoint) {
  Aabb bounds{};
  if (!m_bridge.getActorBounds(actor, bounds)) return hitPoint;

  const float marginX = std::min(0.05f, (bounds.max.x - bounds.min.x) * 0.15f);
  const float marginY = std::min(0.05f, (bounds.max.y - bounds.min.y) * 0.15f);
  const float marginZ = std::min(0.05f, (bounds.max.z - bounds.min.z) * 0.15f);
  Vec3 point{hitPoint.x + randomFloat(-0.05f, 0.05f),
             hitPoint.y + randomFloat(-0.06f, 0.06f),
             hitPoint.z + randomFloat(-0.05f, 0.05f)};
  point.x = clampf(point.x, bounds.min.x + marginX, bounds.max.x - marginX);
  point.y = clampf(point.y, bounds.min.y + marginY, bounds.max.y - marginY);
  point.z = clampf(point.z, bounds.min.z + marginZ, bounds.max.z - marginZ);
  return point;
}

Vec3 AimAssist::smoothedPoint(const Target &target,
                              const Settings &settings) {
  if (m_historyActor != target.actor) {
    m_historyActor = target.actor;
    m_targetHistory.clear();
  }

  // EMA позиции цели по времени, а не окно из N кадров: стабильно при любом FPS.
  const float lag = clampf(static_cast<float>(settings.targetSmoothFrames) / 120.0f,
                           0.0f, 0.25f);
  const float dt = clampf(m_lastDt, 0.0f, kMaxDt);
  if (m_targetHistory.empty() || dt <= 0.0f) {
    if (m_targetHistory.empty()) {
      m_targetHistory.push_back(target.point);
    } else {
      m_targetHistory.back() = target.point;
    }
  } else if (lag > 0.0001f) {
    const float alpha = 1.0f - std::exp(-dt / lag);
    Vec3 &smoothed = m_targetHistory.back();
    smoothed.x += (target.point.x - smoothed.x) * alpha;
    smoothed.y += (target.point.y - smoothed.y) * alpha;
    smoothed.z += (target.point.z - smoothed.z) * alpha;
  } else {
    m_targetHistory.back() = target.point;
  }
  return m_targetHistory.back();
}

bool AimAssist::isInvisible(void *actor) const {
  return gameecs::isInvisible(actor);
}

bool AimAssist::isSameTeam(void *, void *) const {
  // В этой версии team-оффсет не заполнен. Подставь свой team id.
  return false;
}

void AimAssist::clearTarget() {
  m_target = {};
  m_targetHistory.clear();
  m_historyActor = nullptr;
  m_stickyLeft = 0;
  m_targetGrace = 0;
  m_velYaw = 0.0f;
  m_velPitch = 0.0f;
  m_lastAimActor = nullptr;
  m_aimEngageAtMs = 0;
  m_targetLockMs = 0;
  m_serverViewSinceMs = 0;
  m_haveStableAim = false;
  m_stableAimActor = nullptr;
  m_fastStealthActor = nullptr;
  m_nextFastRestMs = 0;
  m_fastTimeScale = 1.0f;
  m_fastSpeedScale = 1.0f;
  m_fastStealthScale = 1.0f;
  m_nextFastPauseMs = 0;
  m_fastPauseUntilMs = 0;
  m_aimPointActor = nullptr;
  m_aimPointOffset = {};
  m_nextAimPointMs = 0;
  m_currentSwitchDelayMs = 0;
  resetHumanProfile();
  // m_onTarget принадлежит триггеру: очистка цели aim assist не должна
  // гасить триггер посреди кадра.
}

void AimAssist::syncLocalFromView(void *player) {
  ViewAngles current{};
  if (!m_bridge.getViewAngles(player, current)) {
    return;
  }
  m_localPitch.store(current.pitch, std::memory_order_relaxed);
  m_localYaw.store(current.yaw, std::memory_order_relaxed);
  m_hasLocal = true;
}

void AimAssist::renderDebug() {
  // На Android нет общего draw-контекста. Отдай DebugState в Java-оверлей
  // клиента и нарисуй там. Этот метод оставлен точкой входа для твоего draw.
}

} // namespace aimbot
