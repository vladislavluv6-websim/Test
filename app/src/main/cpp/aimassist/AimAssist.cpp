#include "AimAssist.hpp"

#include <android/log.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

#include "Offsets.hpp"

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
  m_onTarget.store(false, std::memory_order_release);
  // Release-публикация после сброса состояния: render-поток увидит
  // согласованные поля, а не полу-инициализированные.
  m_enabled.store(true, std::memory_order_release);
  __android_log_print(ANDROID_LOG_INFO, kTag, "module enabled");
}

void AimAssist::onDisable() {
  m_enabled.store(false, std::memory_order_release);
  clearTarget();
  m_velYaw = 0.0f;
  m_velPitch = 0.0f;
  m_onTarget.store(false, std::memory_order_release);
  __android_log_print(ANDROID_LOG_INFO, kTag, "module disabled");
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
  switch (std::max(0, std::min(2, bodyPart))) {
  case 1:
    m_settings.targetBone = TargetBone::Chest;
    break;
  case 2:
    m_settings.targetBone = TargetBone::Legs;
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
    // Плавный, но быстрый критически задемпфированный довод.
    m_settings.smoothingSpeed = 18.0f;
    m_settings.maxDegPerSecond = 650.0f;
    m_settings.maxAccelDegPerSecond2 = 2600.0f;
    m_settings.deadzoneDeg = 0.10f;
    m_settings.fullSpeedRangeDeg = 12.0f;
  } else {
    m_settings.smoothingSpeed = 24.0f;
    m_settings.maxDegPerSecond = 850.0f;
    m_settings.maxAccelDegPerSecond2 = 3600.0f;
    m_settings.deadzoneDeg = 0.08f;
    m_settings.fullSpeedRangeDeg = 14.0f;
  }
}

void AimAssist::setTriggerBot(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerBot = enabled;
  if (!enabled) {
    m_onTarget.store(false, std::memory_order_release);
  }
}

void AimAssist::setTriggerDelay(int delayMs) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerDelayMs = std::max(120, std::min(400, delayMs));
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
  if (!m_enabled.load(std::memory_order_acquire)) {
    return;
  }

  dt = clampf(dt, 0.0f, kMaxDt);
  m_lastDt = dt;

  void *player = m_bridge.getLocalPlayer();
  if (!player) {
    return;
  }

  ViewAngles current{};
  if (!m_bridge.getViewAngles(player, current)) {
    return;
  }

  Vec3 eyeNow{};
  if (m_bridge.getEyePosition(player, eyeNow)) {
    m_eye = eyeNow;
  }

  const Settings settings = getSettings();

  // ActorRotationComponent — фактический view камеры. Синхронизируем local
  // каждый кадр, чтобы ручное движение и ходьба не оставляли устаревший угол.
  m_localPitch.store(current.pitch, std::memory_order_relaxed);
  m_localYaw.store(current.yaw, std::memory_order_relaxed);
  m_hasLocal = true;

  if (!isActive(settings)) {
    clearTarget();
    return;
  }

  // Поиск цели каждый кадр: иначе цель, прошедшая рядом/сбоку, теряется
  // на несколько кадров. Список актёров переиспользует capacity.
  m_frameCounter++;
  findTargetInternal(player, settings);

  if (!m_target.valid) {
    // Цели нет: не двигаем камеру вообще. Скорость гасим плавно,
    // чтобы при появлении цели не было накопленного импульса.
    m_velYaw *= 0.85f;
    m_velPitch *= 0.85f;
    m_onTarget.store(false, std::memory_order_release);
    return;
  }

  // Высота цели задаётся режимом: голова / грудь / ноги.
  m_target.point = getTargetPoint(m_target.actor, settings.targetBone);
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

  const Vec3 point = smoothedPoint(m_target, settings);
  Angles aim = calculateAnglesToPoint(point);

  // Jitter по умолчанию 0. Если включён — обновляем редко, не каждый кадр.
  if (settings.randomJitterDeg > 0.0f) {
    const int64_t now = nowMs();
    if (now - m_lastJitterMs >= settings.randomJitterIntervalMs) {
      m_lastJitterMs = now;
      std::uniform_real_distribution<float> dist(-settings.randomJitterDeg,
                                                 settings.randomJitterDeg);
      m_jitterYaw = dist(m_rng);
      m_jitterPitch = dist(m_rng);
    }
    aim.yaw = normalizeYaw(aim.yaw + m_jitterYaw);
    aim.pitch = clampf(aim.pitch + m_jitterPitch, -89.9f, 89.9f);
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

  // Готовность триггера считаем по реальному углу до цели, не по smoothed.
  const float targetDeltaYaw =
      shortestDelta(afterYaw, m_target.yaw);
  const float targetDeltaPitch = m_target.pitch - afterPitch;
  const float targetAngular =
      std::sqrt(targetDeltaYaw * targetDeltaYaw + targetDeltaPitch * targetDeltaPitch);
  const bool onTarget = settings.triggerBot &&
                        targetAngular <= 2.0f &&
                        m_target.distance <= 3.4f;
  m_onTarget.store(onTarget, std::memory_order_release);

  if (settings.debugOverlay) {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastDebug.time_since_epoch().count() == 0 ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - m_lastDebug)
                .count() >= 1000) {
      m_lastDebug = now;
      __android_log_print(
          ANDROID_LOG_INFO, kTag,
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
  m_networkYaw.store(localYaw, std::memory_order_relaxed);
  m_networkPitch.store(localPitch, std::memory_order_relaxed);

  if (m_stickyLeft > 0) {
    --m_stickyLeft;
  }

  const Settings settings = getSettings();
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

void AimAssist::findTargetInternal(void *player, const Settings &settings) {
  if (!player) return;

  Vec3 eye{};
  if (!m_bridge.getEyePosition(player, eye)) {
    clearTarget();
    return;
  }
  m_eye = eye;

  m_bridge.fillNearbyActors(player, settings.range, m_actors);

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

  Target current = m_target;
  bool currentStillValid = false;

  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;
    const bool playerActor = m_bridge.isPlayer(actor);
    if (playerActor ? !settings.targetPlayers : !settings.targetMobs) continue;
    if (settings.ignoreInvisible && isInvisible(actor)) continue;
    if (settings.teamCheck && isSameTeam(player, actor)) continue;

    Vec3 targetPosition{};
    if (!m_bridge.getPosition(actor, targetPosition)) continue;

    const Vec3 point = getTargetPoint(actor, settings.targetBone);
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
    if (settings.fovDeg < 359.5f && angular > settings.fovDeg) continue;

    if (settings.visibleCheck &&
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

  Target chosen = (settings.targetPriority == TargetPriority::Angle)
                      ? bestAngle
                      : bestDistance;

  bool better = false;
  if (currentStillValid && chosen.valid) {
    const bool directHit = chosen.actor != current.actor &&
                           chosen.angle <= 1.5f;
    if (settings.targetPriority == TargetPriority::Angle) {
      better = directHit ||
               chosen.angle + settings.switchHysteresisDeg < current.angle;
    } else {
      better = directHit ||
               (chosen.distance + 1.0f < current.distance &&
                chosen.angle <= current.angle + 10.0f);
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

Angles AimAssist::calculateAngles(void *entity) const {
  Angles angles{};
  void *player = m_bridge.getLocalPlayer();
  if (!player || !entity) return angles;

  Vec3 eye{};
  if (!m_bridge.getEyePosition(player, eye)) return angles;

  const Settings settings = getSettings();
  const Vec3 point = getTargetPoint(entity, settings.targetBone);
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
  const float horizontal = std::max(0.0001f, std::sqrt(dx * dx + dz * dz));

  angles.yaw = normalizeYaw(std::atan2(-dx, dz) * kRadToDeg);
  angles.pitch =
      clampf(-std::atan2(dy, horizontal) * kRadToDeg, -89.9f, 89.9f);
  return angles;
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

  // strength 0..100 масштабирует скорость, не меняя форму кривой.
  const float strengthScale = clampf(settings.strength / 100.0f, 0.0f, 1.0f);
  const float omega =
      std::max(0.001f, settings.smoothingSpeed * (0.4f + 0.6f * strengthScale));
  const float maxVel =
      std::max(0.0f, settings.maxDegPerSecond * (0.5f + 0.5f * strengthScale));
  const float maxAccel = std::max(
      0.0f, settings.maxAccelDegPerSecond2 * (0.5f + 0.5f * strengthScale));
  const float deadzone = std::max(0.0f, settings.deadzoneDeg);
  const float fullRange = std::max(0.001f, settings.fullSpeedRangeDeg);

  {
    const float curYaw = m_localYaw.load(std::memory_order_relaxed);
    const float curPitch = m_localPitch.load(std::memory_order_relaxed);
    const float deltaYaw = shortestDelta(curYaw, targetYaw);
    const float deltaPitch = targetPitch - curPitch;

    if (std::fabs(deltaYaw) <= deadzone &&
        std::fabs(deltaPitch) <= deadzone) {
      // Не обнуляем скорость мгновенно: плавно тормозим в зоне.
      m_velYaw *= 0.5f;
      m_velPitch *= 0.5f;
      m_deadzoneHit = true;
      m_lastDelta = std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);
      m_lastFactor = 0.0f;
      return;
    }

    const float magnitude =
        std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);
    const float proximity = clampf(magnitude / fullRange, 0.0f, 1.0f);
    // smoothstep: мягкий вход/выход без излома на границе fullSpeedRange.
    const float easedProximity = proximity * proximity * (3.0f - 2.0f * proximity);
    const float localMaxVel = maxVel * easedProximity;

    // Критически задемпфированная пружина: без перелёта и без рывков.
    float accelYaw = omega * omega * deltaYaw - 2.0f * omega * m_velYaw;
    float accelPitch = omega * omega * deltaPitch - 2.0f * omega * m_velPitch;
    if (maxAccel > 0.0f) {
      accelYaw = clampf(accelYaw, -maxAccel, maxAccel);
      accelPitch = clampf(accelPitch, -maxAccel, maxAccel);
    }

    m_velYaw += accelYaw * h;
    m_velPitch += accelPitch * h;
    m_velYaw = clampf(m_velYaw, -localMaxVel, localMaxVel);
    m_velPitch = clampf(m_velPitch, -localMaxVel, localMaxVel);

    float stepYaw = m_velYaw * h;
    float stepPitch = m_velPitch * h;

    // Не проскакиваем цель даже на одном подшаге.
    if (std::fabs(stepYaw) > std::fabs(deltaYaw)) {
      stepYaw = deltaYaw;
      m_velYaw = 0.0f;
    }
    if (std::fabs(stepPitch) > std::fabs(deltaPitch)) {
      stepPitch = deltaPitch;
      m_velPitch = 0.0f;
    }

    const float newYaw = normalizeYaw(curYaw + stepYaw);
    const float newPitch = clampf(curPitch + stepPitch, -89.9f, 89.9f);
    m_localYaw.store(newYaw, std::memory_order_relaxed);
    m_localPitch.store(newPitch, std::memory_order_relaxed);

    m_lastDelta = magnitude;
    m_lastFactor = (maxVel > 0.0001f)
                       ? std::sqrt(m_velYaw * m_velYaw +
                                   m_velPitch * m_velPitch) /
                             maxVel
                       : 0.0f;
    m_deadzoneHit = false;
  }
}

// ============================================================
// Вспомогательное
// ============================================================
Vec3 AimAssist::getTargetPoint(void *actor, TargetBone bone) const {
  Aabb bounds{};
  if (!m_bridge.getActorBounds(actor, bounds)) {
    Vec3 position{};
    m_bridge.getPosition(actor, position);
    return position;
  }

  // Точка считается от низа реального хитбокса. Раньше к StateVector-позиции
  // прибавлялось +1.62, но у игроков эта позиция уже на уровне глаз, а у
  // мобов — у ног; из-за этого прицел уходил вниз и не поднимался к голове.
  float fraction = offsets::kHeadFraction;
  switch (bone) {
  case TargetBone::Chest:
    fraction = offsets::kChestFraction;
    break;
  case TargetBone::Legs:
    fraction = offsets::kLegsFraction;
    break;
  case TargetBone::Head:
  default:
    fraction = offsets::kHeadFraction;
    break;
  }

  Vec3 point{};
  point.x = (bounds.min.x + bounds.max.x) * 0.5f;
  point.z = (bounds.min.z + bounds.max.z) * 0.5f;
  point.y = bounds.min.y + (bounds.max.y - bounds.min.y) * fraction;
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
  // В 1.26.60 оффсет flags не заполнен. Если знаешь его — читай здесь
  // flags и проверяй offsets::kFlagInvisible.
  (void)actor;
  return false;
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
  m_onTarget.store(false, std::memory_order_release);
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
