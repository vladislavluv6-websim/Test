#include "AimAssist.hpp"

#include <android/log.h>

#include "DiagLog.hpp"

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
  resetHumanProfile();
  m_onTarget.store(false, std::memory_order_release);
  // Release-публикация после сброса состояния: render-поток увидит
  // согласованные поля, а не полу-инициализированные.
  m_enabled.store(true, std::memory_order_release);
  aimbot::diagLog(kTag, "module enabled");
}

void AimAssist::onDisable() {
  m_enabled.store(false, std::memory_order_release);
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
    // Мягкий довод с заметным лагом и «мёртвой зоной»: камера не прилипает
    // к центру хитбокса и не повторяет одинаковые шаги каждый кадр.
    m_settings.smoothingSpeed = 15.0f;
    m_settings.maxDegPerSecond = 560.0f;
    m_settings.maxAccelDegPerSecond2 = 2200.0f;
    m_settings.deadzoneDeg = 0.30f;
    m_settings.fullSpeedRangeDeg = 18.0f;
    m_settings.targetSmoothFrames = 6;
  } else {
    m_settings.smoothingSpeed = 24.0f;
    m_settings.maxDegPerSecond = 900.0f;
    m_settings.maxAccelDegPerSecond2 = 3600.0f;
    m_settings.deadzoneDeg = 0.18f;
    m_settings.fullSpeedRangeDeg = 16.0f;
    m_settings.targetSmoothFrames = 3;
  }
}

void AimAssist::setTriggerBot(bool enabled) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.triggerBot = enabled;
  m_triggerSinceActor = nullptr;
  m_triggerSinceMs = 0;
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
  m_settings.triggerDelayMs = std::max(120, std::min(400, delayMs));
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
  m_settings.assistConeDeg = clampf(degrees, 10.0f, 180.0f);
}

void AimAssist::setAttackMissChance(float chance) {
  std::lock_guard<std::mutex> lock(m_settingsMutex);
  m_settings.attackMissChance = clampf(chance, 0.0f, 0.35f);
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
  if (visualCall <= 3 || (visualCall % 600) == 0) {
    aimbot::diagLog(kTag, "updateVisual#%llu client=%p player=%p",
                    static_cast<unsigned long long>(visualCall),
                    m_bridge.getClientInstance(), player);
  }
  if (!player) {
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
    m_bridge.fillNearbyActors(player, actorRange, m_actors);
  }
  if (settings.triggerBot && haveEye) {
    updateTrigger(player, settings, current, eyeNow);
  } else {
    m_onTarget.store(false, std::memory_order_release);
    m_triggerActor.store(nullptr, std::memory_order_release);
    m_triggerDistance.store(0.0f, std::memory_order_relaxed);
  }

  // Диагностика: раз в ~2 секунды (120 кадров) пишем состояние триггера.
  // Помогает понять, где именно обрывается цепочка на устройстве.
  static uint64_t s_diagFrame = 0;
  if ((++s_diagFrame % 120) == 0) {
    aimbot::diagLog(kTag,
        "diag player=%p view=(%.1f,%.1f) eye=(%.2f,%.2f,%.2f) actors=%zu "
        "trigger=%d range=%.2f target=%p onTarget=%d gm=%p",
        player, static_cast<double>(current.pitch),
        static_cast<double>(current.yaw), static_cast<double>(eyeNow.x),
        static_cast<double>(eyeNow.y), static_cast<double>(eyeNow.z),
        m_actors.size(), settings.triggerBot ? 1 : 0,
        static_cast<double>(settings.triggerRange),
        m_triggerActor.load(std::memory_order_acquire),
        m_onTarget.load(std::memory_order_acquire) ? 1 : 0,
        m_bridge.getGameMode());
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
    resetHumanProfile();
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

  const int64_t aimNow = nowMs();

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

  // Одноразовый перелёт: на захвате цель «пролетается» на несколько
  // процентов начальной ошибки и затем доводится. Живая рука не тормозит
  // точно в центре с первого движения.
  if (m_targetLockMs == 0) {
    m_targetLockMs = aimNow;
    if (settings.humanize && settings.overshootFactor > 0.0f) {
      const float curYaw = m_localYaw.load(std::memory_order_relaxed);
      const float curPitch = m_localPitch.load(std::memory_order_relaxed);
      const float dy = shortestDelta(curYaw, aim.yaw);
      const float dp = aim.pitch - curPitch;
      const float jitter = randomFloat(0.6f, 1.35f);
      m_overYaw = clampf(dy * settings.overshootFactor * jitter, -6.0f, 6.0f);
      m_overPitch =
          clampf(dp * settings.overshootFactor * jitter, -4.0f, 4.0f);
    }
  }

  if (settings.humanize) {
    updateHumanProfile(settings, dt, aimNow);
    aim.yaw = normalizeYaw(aim.yaw + m_restYaw + m_noiseYaw + m_overYaw);
    aim.pitch = clampf(
        aim.pitch + m_restPitch + m_noisePitch + m_overPitch, -89.9f, 89.9f);
  }

  // Jitter по умолчанию 0. Если включён — обновляем редко, не каждый кадр.
  if (settings.randomJitterDeg > 0.0f) {
    if (aimNow - m_lastJitterMs >= settings.randomJitterIntervalMs) {
      m_lastJitterMs = aimNow;
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
  const float assistFov =
      settings.antiCheatMode ? std::min(settings.fovDeg, settings.assistConeDeg)
                             : settings.fovDeg;

  Target current = m_target;
  bool currentStillValid = false;

  // Актёр, в чей хитбокс сейчас упирается прицел (посчитан updateTrigger).
  // Его aim assist обязан вести первым: иначе доводка утащит камеру с
  // хитбокса и триггер перестанет бить.
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
    if (assistFov < 359.5f && angular > assistFov) continue;

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

  Target chosen = (settings.targetPriority == TargetPriority::Angle)
                      ? bestAngle
                      : bestDistance;

  // Приоритет цели триггера: не даём aim assist увести прицел с хитбокса.
  const bool triggerOverride = triggerTarget.valid;
  if (triggerOverride) {
    chosen = triggerTarget;
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
    if (!directHit && settings.switchDelayMs > 0 && m_lastSwitchMs != 0 &&
        switchNow - m_lastSwitchMs < settings.switchDelayMs) {
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
void AimAssist::updateTrigger(void *player, const Settings &settings,
                              const ViewAngles &view, const Vec3 &eye) {
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
  float minAngleDist = 0.0f;
  Vec3 minAngleCenter{};
  void *minAngleActor = nullptr;

  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;

    const bool playerActor = m_bridge.isPlayer(actor);
    if (playerActor ? !settings.targetPlayers : !settings.targetMobs) continue;
    if (settings.ignoreInvisible && isInvisible(actor)) continue;
    if (settings.teamCheck && isSameTeam(player, actor)) continue;

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
        const float angle = std::acos(dot) * kRadToDeg;
        if (angle < minAngle) {
          minAngle = angle;
          minAngleDist = len;
          minAngleCenter = center;
          minAngleActor = actor;
        }
      }
    }
    ++tested;

    // Допуск по горизонтали: позиция актёра интерполируется, и хитбокс
    // может отставать от фактического положения на кадр-два.
    bounds.min.x -= tolerance;
    bounds.min.z -= tolerance;
    bounds.max.x += tolerance;
    bounds.max.z += tolerance;
    bounds.min.y -= tolerance * 0.5f;
    bounds.max.y += tolerance * 0.5f;

    float hit = 0.0f;
    if (!rayIntersectsAabb(eye, dir, bounds, bestDistance, hit)) continue;
    if (hit >= bestDistance) continue;

    bestDistance = hit;
    bestActor = actor;
  }

  // Мягкое добивание допустимо только в не-legit режиме и только когда
  // прицел почти на цели (малый угол), а не «ближайший в радиусе». Удар
  // при взгляде в сторону — прямой триггер aim/killaura-проверок
  // (Polar, FairPlay AimbotTargetOffset, GhostAC aim window).
  bool angleFallback = false;
  if (!bestActor && minAngleActor && minAngleDist <= reach &&
      !settings.legitAttackOnly && minAngle <= settings.attackMaxAngleDeg) {
    bestActor = minAngleActor;
    bestDistance = minAngleDist;
    angleFallback = true;
  }

  static uint64_t s_triggerLog = 0;
  if ((++s_triggerLog % 60) == 1) {
    aimbot::diagLog(
        kTag,
        "trigger actors=%zu tested=%d noBounds=%d dir=(%.2f,%.2f,%.2f) "
        "eye=(%.2f,%.2f,%.2f) minAng=%.1f minDist=%.2f center=(%.2f,%.2f,%.2f) "
        "minActor=%p best=%p bestDist=%.2f reach=%.2f p=%d m=%d",
        m_actors.size(), tested, skippedBounds, static_cast<double>(dir.x),
        static_cast<double>(dir.y), static_cast<double>(dir.z),
        static_cast<double>(eye.x), static_cast<double>(eye.y),
        static_cast<double>(eye.z), static_cast<double>(minAngle),
        static_cast<double>(minAngleDist), static_cast<double>(minAngleCenter.x),
        static_cast<double>(minAngleCenter.y),
        static_cast<double>(minAngleCenter.z), minAngleActor, bestActor,
        static_cast<double>(bestDistance), static_cast<double>(reach),
        settings.targetPlayers ? 1 : 0, settings.targetMobs ? 1 : 0);
  }

  m_triggerActor.store(bestActor, std::memory_order_release);
  m_triggerDistance.store(bestActor ? bestDistance : 0.0f,
                          std::memory_order_relaxed);
  m_onTarget.store(bestActor != nullptr, std::memory_order_release);

  if (!bestActor) {
    m_triggerSinceActor = nullptr;
    m_triggerSinceMs = 0;
    return;
  }

  // Атака выполняется прямо здесь, на игровом потоке: тот же путь, что у
  // внутренних читов (Killaura/Aura) — GameMode::attack, без инъекции
  // касаний и без участия UI-потока.
  const int64_t now = monotonicMs();
  if (m_triggerSinceActor != bestActor) {
    m_triggerSinceActor = bestActor;
    m_triggerSinceMs = now;
    // Реакция на новую цель — со случайным разбросом, а не константа.
    m_triggerDelayForTargetMs = settings.triggerDelayMs;
    if (settings.humanize && settings.triggerDelayJitterMs > 0) {
      m_triggerDelayForTargetMs +=
          randomInt(0, settings.triggerDelayJitterMs);
    }
  }
  if (now - m_triggerSinceMs < m_triggerDelayForTargetMs) return;
  if (m_nextAttackAtMs != 0 && now < m_nextAttackAtMs) return;

  // Не бьём сразу после смены цели и иногда «промахиваемся» (не жмём).
  if (settings.humanize) {
    if (m_lastSwitchMs != 0 && now - m_lastSwitchMs < 120) return;
    if (settings.attackMissChance > 0.0f &&
        randomFloat(0.0f, 1.0f) < settings.attackMissChance) {
      m_nextAttackAtMs = now + randomInt(80, 220);
      return;
    }
  }

  Vec3 hitPoint{};
  if (angleFallback) {
    hitPoint = minAngleCenter;
  } else {
    hitPoint.x = eye.x + dir.x * bestDistance;
    hitPoint.y = eye.y + dir.y * bestDistance;
    hitPoint.z = eye.z + dir.z * bestDistance;
  }

  {
    static uint64_t s_fireLog = 0;
    const uint64_t n = ++s_fireLog;
    if (n <= 5 || (n % 20) == 0) {
      aimbot::diagLog(kTag, "trigger fire #%llu actor=%p dist=%.2f gm=%p",
                      static_cast<unsigned long long>(n), bestActor,
                      static_cast<double>(bestDistance),
                      m_bridge.getGameMode());
    }
  }
  if (m_bridge.attackActor(bestActor, hitPoint)) {
    // Интервал между ударами: база + разброс + редкая «задумчивость».
    int64_t next = now + settings.triggerIntervalMs;
    if (settings.attackIntervalJitterMs > 0) {
      next += randomInt(-settings.attackIntervalJitterMs,
                        settings.attackIntervalJitterMs);
    }
    if (settings.humanize && randomFloat(0.0f, 1.0f) < 0.08f) {
      next += randomInt(120, 320);
    }
    m_nextAttackAtMs = std::max(now + 120, next);
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

  // Медленная вариация «мотора»: скорость и ускорение не постоянны от
  // секунды к секунде. Убирает одинаковые шаги поворота (проверки
  // step modulo / step quotient у FairPlay и аналогов).
  const float motor = clampf(m_motorScale, 0.5f, 1.5f);

  {
    const float curYaw = m_localYaw.load(std::memory_order_relaxed);
    const float curPitch = m_localPitch.load(std::memory_order_relaxed);
    const float deltaYaw = shortestDelta(curYaw, targetYaw);
    const float deltaPitch = targetPitch - curPitch;

    // Гистерезис мёртвой зоны: войти в неё сложнее, чем остаться. Не даёт
    // камере «зудеть» на границе и генерировать пачки микродельт.
    const float enter = deadzone;
    const float stay = deadzone * 1.7f;
    const float limit = m_deadzoneHit ? stay : enter;
    if (std::fabs(deltaYaw) <= limit && std::fabs(deltaPitch) <= limit) {
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
    float localMaxVel = maxVel * motor * easedProximity;
    // Ограничение поворота на серверный тик: даже при максимальной
    // скорости не выдаём «телепорт» больше maxTickDeltaDeg за 50 мс.
    if (settings.maxTickDeltaDeg > 0.0f) {
      localMaxVel = std::min(localMaxVel, settings.maxTickDeltaDeg / 0.05f);
    }

    // Слегка недодемпфированная пружина (zeta ~0.88): довод имеет
    // естественный перелёт на 1-3%, который затем гасится. Идеально
    // критический довод даёт «прилипание» и подозрительно нулевую ошибку.
    const float damping = 1.76f * omega;
    float accelYaw = omega * omega * deltaYaw - damping * m_velYaw;
    float accelPitch = omega * omega * deltaPitch - damping * m_velPitch;
    if (maxAccel > 0.0f) {
      const float maxAccelScaled = maxAccel * motor;
      accelYaw = clampf(accelYaw, -maxAccelScaled, maxAccelScaled);
      accelPitch = clampf(accelPitch, -maxAccelScaled, maxAccelScaled);
    }

    m_velYaw += accelYaw * h;
    m_velPitch += accelPitch * h;
    m_velYaw = clampf(m_velYaw, -localMaxVel, localMaxVel);
    m_velPitch = clampf(m_velPitch, -localMaxVel, localMaxVel);

    float stepYaw = m_velYaw * h;
    float stepPitch = m_velPitch * h;

    if (settings.maxTickDeltaDeg > 0.0f) {
      const float stepCap = settings.maxTickDeltaDeg * (h / 0.05f);
      stepYaw = clampf(stepYaw, -stepCap, stepCap);
      stepPitch = clampf(stepPitch, -stepCap, stepCap);
    }

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
  m_overYaw = 0.0f;
  m_overPitch = 0.0f;
  m_motorScale = 1.0f;
  m_nextMotorMs = 0;
  m_nextRestMs = 0;
  m_targetLockMs = 0;
}

void AimAssist::updateHumanProfile(const Settings &settings, float dt,
                                   int64_t now) {
  dt = clampf(dt, 0.0f, kMaxDt);

  // Медленная вариация силы/скорости движения руки.
  if (now >= m_nextMotorMs) {
    const float var = clampf(settings.motorVariation, 0.0f, 0.45f);
    m_motorScale = randomFloat(1.0f - var, 1.0f + var);
    m_nextMotorMs = now + randomInt(180, 480);
  }

  // Gauss-Markov (OU) шум сопровождения. В отличие от синусоид он не
  // периодичен, но и не «дребезжит» каждый кадр: соседние значения
  // коррелированы, поэтому детекторы jitter/направлений не видят
  // искусственно высокой частоты смены знака.
  const float sigma =
      std::max(0.0f, settings.trackingNoiseDeg + settings.aimTremorDeg);
  if (sigma > 0.0001f && dt > 0.0f) {
    const float tau = 0.55f;
    const float a = dt / tau;
    const float k = sigma * std::sqrt(2.0f * a);
    std::normal_distribution<float> normal(0.0f, 1.0f);
    m_noiseYaw += -m_noiseYaw * a + normal(m_rng) * k;
    m_noisePitch += -m_noisePitch * a + normal(m_rng) * k * 0.7f;
    const float limit = sigma * 2.5f;
    m_noiseYaw = clampf(m_noiseYaw, -limit, limit);
    m_noisePitch = clampf(m_noisePitch, -limit, limit);
  } else {
    m_noiseYaw = 0.0f;
    m_noisePitch = 0.0f;
  }

  // Точка покоя не в математическом центре хитбокса и медленно блуждает.
  if (now >= m_nextRestMs) {
    const float rest = std::max(0.0f, settings.restOffsetDeg);
    m_restYaw = randomFloat(-rest, rest);
    m_restPitch = randomFloat(-rest, rest) * 0.6f;
    m_nextRestMs = now + randomInt(500, 1400);
  }

  // Перелёт затухает за ~0.18 с — это скорость коррекции живой руки.
  const float decay = std::exp(-dt / 0.18f);
  m_overYaw *= decay;
  m_overPitch *= decay;
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
  m_lastAimActor = nullptr;
  m_aimEngageAtMs = 0;
  m_targetLockMs = 0;
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
