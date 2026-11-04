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
  m_enabled.store(true, std::memory_order_release);
  m_hasLocal = false;
  m_target = {};
  m_targetHistory.clear();
  m_historyActor = nullptr;
  m_stickyLeft = 0;
  m_frameCounter = 0;
  m_onTarget.store(false, std::memory_order_release);
  __android_log_print(ANDROID_LOG_INFO, kTag, "module enabled");
}

void AimAssist::onDisable() {
  m_enabled.store(false, std::memory_order_release);
  clearTarget();
  m_onTarget.store(false, std::memory_order_release);
  __android_log_print(ANDROID_LOG_INFO, kTag, "module disabled");
}

void AimAssist::onTick() { updateNetwork(); }

void AimAssist::onRender(float dt) { updateVisual(dt); }

void AimAssist::onConfigLoad() {
  // Здесь можно вызвать конфиг-систему клиента.
  // В текущем APK настройки приходят через setRange/setStrength/setLegitMode.
  m_hasLocal = false;
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
  m_settings.range = clampf(range, 3.0f, 16.0f);
}

void AimAssist::setStrength(float strength) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.strength = clampf(strength, 0.0f, 100.0f);
}

void AimAssist::setLegitMode(bool legit) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.legit = legit;
  if (legit) {
    m_settings.smoothingSpeed = 14.0f;
    m_settings.maxDegPerSecond = 240.0f;
    m_settings.deadzoneDeg = 0.5f;
    m_settings.fullSpeedRangeDeg = 8.0f;
  } else {
    m_settings.smoothingSpeed = 22.0f;
    m_settings.maxDegPerSecond = 520.0f;
    m_settings.deadzoneDeg = 0.2f;
    m_settings.fullSpeedRangeDeg = 12.0f;
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

void AimAssist::setTouchActive(bool active) {
  m_touchActive.store(active, std::memory_order_release);
  if (active) {
    const Settings settings = getSettings();
    m_humanPauseUntilMs = nowMs() + std::max(0, settings.humanizePauseMs);
  }
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

bool AimAssist::isPauseActive(const Settings &) const {
  if (m_touchActive.load(std::memory_order_acquire)) {
    return true;
  }
  return nowMs() < m_humanPauseUntilMs;
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
  if (m_bridge.getPosition(player, eyeNow)) {
    eyeNow.y += offsets::kEyeHeight;
    m_eye = eyeNow;
  }

  const Settings settings = getSettings();

  if (!m_hasLocal) {
    m_localPitch.store(current.pitch, std::memory_order_relaxed);
    m_localYaw.store(current.yaw, std::memory_order_relaxed);
    m_hasLocal = true;
  }

  // Если компонент view изменился (ваниль/палец/applyTurnDelta), подхватываем.
  // Если applyTurnDelta двигает только камеру, компонент не меняется —
  // тогда продолжаем вести виртуальные local-углы.
  if (!m_hasReadView) {
    m_lastReadYaw = current.yaw;
    m_lastReadPitch = current.pitch;
    m_hasReadView = true;
  } else {
    const float changedYaw =
        std::fabs(shortestDelta(m_lastReadYaw, current.yaw));
    const float changedPitch = std::fabs(current.pitch - m_lastReadPitch);
    if (changedYaw > 0.001f || changedPitch > 0.001f) {
      m_localYaw.store(current.yaw, std::memory_order_relaxed);
      m_localPitch.store(current.pitch, std::memory_order_relaxed);
      m_hasLocal = true;
    }
    m_lastReadYaw = current.yaw;
    m_lastReadPitch = current.pitch;
  }

  if (!isActive(settings)) {
    clearTarget();
    return;
  }

  if (isPauseActive(settings)) {
    return;
  }

  // Поиск цели не каждый кадр: дорогой обход entity list.
  m_frameCounter++;
  if (!m_target.valid || (m_frameCounter % 3) == 0) {
    findTargetInternal(player, settings);
  }

  if (!m_target.valid) {
    m_onTarget.store(false, std::memory_order_release);
    return;
  }

  // Обновляем точку текущей цели каждый кадр — это дешёвое чтение позиции.
  Vec3 position{};
  if (!m_bridge.getPosition(m_target.actor, position)) {
    clearTarget();
    return;
  }
  m_target.point = getTargetPoint(m_target.actor, settings.targetBone);
  const Angles latest = calculateAnglesToPoint(m_target.point);
  m_target.yaw = latest.yaw;
  m_target.pitch = latest.pitch;
  const float dxTarget = m_target.point.x - m_eye.x;
  const float dyTarget = m_target.point.y - m_eye.y;
  const float dzTarget = m_target.point.z - m_eye.z;
  m_target.distance = std::sqrt(dxTarget * dxTarget + dyTarget * dyTarget +
                                dzTarget * dzTarget);

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
  if (std::fabs(stepYaw) > 0.0001f || std::fabs(stepPitch) > 0.0001f) {
    // applyTurnDelta двигает локальную камеру. Знак pitchUp: минус — вверх.
    m_bridge.applyTurnDelta(player, -stepPitch, stepYaw);
    // Body yaw синхронизируем, pitch компонента сохраняем.
    m_bridge.setBodyYaw(player, afterYaw);
  }

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
          "factor=%.3f dt=%.4f deadzone=%d target=%p",
          afterPitch, afterYaw, m_target.pitch, m_target.yaw, m_lastDelta,
          m_lastFactor, m_lastDt, m_deadzoneHit ? 1 : 0, m_target.actor);
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
  if (settings.freezeNetwork) {
    return;
  }

  void *player = m_bridge.getLocalPlayer();
  if (!player) return;

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
  if (!m_bridge.getPosition(player, eye)) {
    clearTarget();
    return;
  }
  eye.y += offsets::kEyeHeight;
  m_eye = eye;

  m_bridge.fillNearbyActors(player, settings.range, m_actors);

  Target bestAngle{};
  Target bestDistance{};
  bestAngle.angle = FLT_MAX;
  bestDistance.distance = FLT_MAX;

  const float localYaw = m_localYaw.load(std::memory_order_relaxed);
  const float localPitch = m_localPitch.load(std::memory_order_relaxed);

  Target current = m_target;
  bool currentStillValid = false;

  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;
    if (!m_bridge.isPlayer(actor)) continue;
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
    if (angular > settings.fovDeg) continue;

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

  // Sticky target: пока тики не вышли, держим текущую цель.
  if (currentStillValid && m_stickyLeft > 0) {
    m_target = current;
    m_target.valid = true;
    return;
  }

  bool better = false;
  if (currentStillValid && chosen.valid) {
    if (settings.targetPriority == TargetPriority::Angle) {
      better = chosen.angle + settings.switchHysteresisDeg < current.angle;
    } else {
      better = chosen.distance + 1.0f < current.distance &&
               chosen.angle <= current.angle + 10.0f;
    }
  }

  if (currentStillValid && !better) {
    m_target = current;
    m_target.valid = true;
    return;
  }

  if (chosen.valid) {
    m_target = chosen;
    m_stickyLeft = std::max(0, settings.stickyTicks);
    m_target.valid = true;
  } else if (currentStillValid) {
    m_target = current;
    m_target.valid = true;
  } else {
    clearTarget();
  }
}

Angles AimAssist::calculateAngles(void *entity) const {
  Angles angles{};
  void *player = m_bridge.getLocalPlayer();
  if (!player || !entity) return angles;

  Vec3 eye{};
  if (!m_bridge.getPosition(player, eye)) return angles;
  eye.y += offsets::kEyeHeight;

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

  const float currentYaw = m_localYaw.load(std::memory_order_relaxed);
  const float currentPitch = m_localPitch.load(std::memory_order_relaxed);

  const float deltaYaw = shortestDelta(currentYaw, targetYaw);
  float deltaPitch = targetPitch - currentPitch;

  if (std::fabs(deltaYaw) < settings.deadzoneDeg &&
      std::fabs(deltaPitch) < settings.deadzoneDeg) {
    m_deadzoneHit = true;
    m_lastDelta = std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);
    m_lastFactor = 0.0f;
    return;
  }

  const float magnitude =
      std::sqrt(deltaYaw * deltaYaw + deltaPitch * deltaPitch);
  const float factor = 1.0f - std::exp(-settings.smoothingSpeed * dt);
  const float range = std::max(0.001f, settings.fullSpeedRangeDeg);
  const float proximity = clampf(magnitude / range, 0.0f, 1.0f);
  const float localFactor = factor * proximity;
  const float maxStep = settings.maxDegPerSecond * dt;

  float stepYaw = clampf(deltaYaw * localFactor, -maxStep, maxStep);
  float stepPitch = clampf(deltaPitch * localFactor, -maxStep, maxStep);

  // Микро-квантование убирает одинаковые дельты, но не даёт дрожи.
  constexpr float kQuantum = 0.01f;
  stepYaw = std::round(stepYaw / kQuantum) * kQuantum;
  stepPitch = std::round(stepPitch / kQuantum) * kQuantum;

  if (std::fabs(stepYaw) < 0.0001f && std::fabs(stepPitch) < 0.0001f) {
    m_deadzoneHit = true;
    return;
  }

  const float newYaw = normalizeYaw(currentYaw + stepYaw);
  const float newPitch = clampf(currentPitch + stepPitch, -89.9f, 89.9f);

  m_localYaw.store(newYaw, std::memory_order_relaxed);
  m_localPitch.store(newPitch, std::memory_order_relaxed);

  m_lastDelta = magnitude;
  m_lastFactor = localFactor;
  m_deadzoneHit = false;
}

// ============================================================
// Вспомогательное
// ============================================================
Vec3 AimAssist::getTargetPoint(void *actor, TargetBone bone) const {
  Vec3 position{};
  if (!m_bridge.getPosition(actor, position)) {
    return position;
  }

  switch (bone) {
  case TargetBone::Head:
    position.y += offsets::kHeadOffset;
    break;
  case TargetBone::Chest:
    position.y += offsets::kChestOffset;
    break;
  case TargetBone::Nearest:
  default:
    position.y += offsets::kNearestOffset;
    break;
  }
  return position;
}

Vec3 AimAssist::smoothedPoint(const Target &target,
                              const Settings &settings) {
  if (m_historyActor != target.actor) {
    m_historyActor = target.actor;
    m_targetHistory.clear();
  }

  m_targetHistory.push_back(target.point);
  const size_t maxFrames =
      static_cast<size_t>(std::max(1, settings.targetSmoothFrames));
  while (m_targetHistory.size() > maxFrames) {
    m_targetHistory.pop_front();
  }

  Vec3 sum{};
  for (const Vec3 &point : m_targetHistory) {
    sum.x += point.x;
    sum.y += point.y;
    sum.z += point.z;
  }
  const float inv = 1.0f / static_cast<float>(m_targetHistory.size());
  return Vec3{sum.x * inv, sum.y * inv, sum.z * inv};
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
