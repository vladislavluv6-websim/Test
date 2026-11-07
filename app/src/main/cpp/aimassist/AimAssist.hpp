#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <mutex>
#include <random>
#include <vector>

#include "ClientBridge.hpp"
#include "ModuleBase.hpp"

namespace aimbot {

struct Angles {
  float pitch = 0.0f;
  float yaw = 0.0f;
};

enum class ActivationMode : int {
  Always = 0,
  Key = 1,
  Attack = 2,
};

enum class TargetPriority : int {
  Angle = 0,
  Distance = 1,
};

enum class TargetBone : int {
  Head = 0,
  Chest = 1,
  Legs = 2,
};

struct Settings {
  bool enabled = false;

  ActivationMode activationMode = ActivationMode::Always;
  int activationKey = 0;

  float fovDeg = 360.0f;
  float range = 11.68f;
  float smoothingSpeed = 18.0f;
  float maxDegPerSecond = 650.0f;
  float maxAccelDegPerSecond2 = 2600.0f;
  float deadzoneDeg = 0.10f;
  float fullSpeedRangeDeg = 12.0f;

  TargetPriority targetPriority = TargetPriority::Angle;
  TargetBone targetBone = TargetBone::Head;

  int stickyTicks = 4;
  float switchHysteresisDeg = 3.0f;
  int targetSmoothFrames = 0;
  int targetGraceFrames = 12;

  float randomJitterDeg = 0.0f;
  int randomJitterIntervalMs = 150;

  bool visibleCheck = true;
  bool ignoreInvisible = true;
  bool teamCheck = false;
  bool targetPlayers = true;
  bool targetMobs = true;
  bool debugOverlay = false;
  bool freezeNetwork = false;
  // Синхронизация body yaw на сетевом тике. По умолчанию выключена:
  // applyTurnDelta уже двигает view, а лишняя запись компонента даёт дрожание.
  bool syncBodyYaw = false;

  // Совместимость с текущим UI клиента.
  bool legit = true;
  float strength = 100.0f;
  bool triggerBot = false;
  int triggerDelayMs = 250;

  // Триггер работает по пересечению луча прицела с хитбоксом, а не по
  // углу до цели aim assist. Дальность — до поверхности хитбокса.
  // Ванильный предел атаки в Bedrock — 3.0 блока от глаз.
  float triggerRange = 3.0f;
  // Допуск по хитбоксу: компенсирует интерполяцию позиции между тиками
  // и расхождение клиент/сервер. Увеличен для стабильной работы.
  float triggerBoxTolerance = 0.05f;

  // ------------------------------------------------------------
  // Humanization / устойчивость к серверным анти-читам
  // (Polar, Matrix/ML, Geyser-прокси и т.п.)
  // ------------------------------------------------------------
  // Реакция перед началом доводки: живой игрок не начинает вести цель
  // в тот же кадр, в котором она появилась.
  bool humanize = true;
  float reactionDelayMs = 130.0f;
  float reactionJitterMs = 80.0f;
  // Микро-тремор камеры. Ломает «идеально прямые» траектории, которые
  // ищут эвристики анти-читов.
  float aimTremorDeg = 0.02f;
  // Верхний предел поворота за один серверный тик (50 мс). Защита от
  // «телепорт-снапов», которые ловят rotation-check'и.
  float maxTickDeltaDeg = 40.0f;
};

struct DebugState {
  bool enabled = false;
  bool hasTarget = false;
  bool onTarget = false;
  bool deadzoneHit = false;
  bool freezeNetwork = false;

  float localPitch = 0.0f;
  float localYaw = 0.0f;
  float targetPitch = 0.0f;
  float targetYaw = 0.0f;
  float delta = 0.0f;
  float factor = 0.0f;
  float dt = 0.0f;

  void *target = nullptr;
  float targetDistance = 0.0f;
};

class AimAssist : public ModuleBase {
public:
  static AimAssist &instance();

  // ModuleBase
  void onEnable() override;
  void onDisable() override;
  void onTick() override;
  void onRender(float dt) override;
  void onConfigLoad() override;

  // Публичный API из ТЗ.
  void updateVisual(float dt);
  void updateNetwork();
  void findTarget();
  Angles calculateAngles(void *entity) const;
  void applySmoothRotation(float targetYaw, float targetPitch, float dt);
  void renderDebug();

  // JNI / UI.
  void setEnabled(bool enabled);
  bool isEnabled() const;
  void setRange(float range);
  void setStrength(float strength);
  void setFov(float fovDeg);
  void setWallCheck(bool enabled);
  void setTargetPlayers(bool enabled);
  void setTargetMobs(bool enabled);
  void setBodyPart(int bodyPart);
  void setMaxSpeed(float degPerSecond);
  void setSmoothing(float speed);
  void setAcceleration(float degPerSecond2);
  void setDeadzone(float deg);
  void setFullSpeedRange(float deg);
  void setStickyTicks(int ticks);
  void setTargetGrace(int frames);
  void setPreferClosest(bool preferClosest);
  void setLegitMode(bool legit);
  void setTriggerBot(bool enabled);
  void setTriggerDelay(int delayMs);
  void setTriggerRange(float range);
  void setTriggerTolerance(float tolerance);
  void setHumanization(bool enabled);
  void setReactionDelay(int baseMs, int jitterMs);
  void setAimTremor(float degrees);
  void setTouchActive(bool active);
  void notifyCameraLook();
  void setActivationHeld(bool held);
  bool isOnTarget() const;

  Settings getSettings() const;
  void setSettings(const Settings &settings);
  DebugState getDebugState() const;

private:
  struct Target {
    void *actor = nullptr;
    Vec3 point{};
    float distance = 0.0f;
    float angle = 0.0f;
    float yaw = 0.0f;
    float pitch = 0.0f;
    bool valid = false;
  };

  AimAssist() = default;

  bool isActive(const Settings &settings) const;
  bool isInvisible(void *actor) const;
  bool isSameTeam(void *local, void *other) const;
  Vec3 getTargetPoint(void *actor, TargetBone bone) const;
  Angles calculateAnglesToPoint(const Vec3 &point) const;
  void findTargetInternal(void *player, const Settings &settings,
                          bool actorsAlreadyFilled = false);
  void updateTrigger(void *player, const Settings &settings,
                     const ViewAngles &view, const Vec3 &eye);
  void applySmoothRotationInternal(float targetYaw, float targetPitch,
                                   float dt, const Settings &settings);
  void clearTarget();
  void syncLocalFromView(void *player);
  Vec3 smoothedPoint(const Target &target, const Settings &settings);

  ClientBridge &m_bridge = ClientBridge::instance();

  mutable std::mutex m_settingsMutex;
  Settings m_settings;

  std::atomic<bool> m_enabled{false};
  std::atomic<bool> m_activationHeld{false};
  std::atomic<float> m_localYaw{0.0f};
  std::atomic<float> m_localPitch{0.0f};
  std::atomic<float> m_networkYaw{0.0f};
  std::atomic<float> m_networkPitch{0.0f};
  std::atomic<bool> m_onTarget{false};

  // Цель под прицелом (ray-vs-hitbox), независимая от aim assist.
  std::atomic<void *> m_triggerActor{nullptr};
  std::atomic<float> m_triggerDistance{0.0f};

  bool m_hasLocal = false;

  Target m_target;
  std::vector<void *> m_actors;
  std::deque<Vec3> m_targetHistory;
  void *m_historyActor = nullptr;

  int m_frameCounter = 0;
  int m_stickyLeft = 0;
  int m_targetGrace = 0;
  int64_t m_lastJitterMs = 0;
  // Humanization state.
  int64_t m_aimEngageAtMs = 0;
  void *m_lastAimActor = nullptr;
  float m_tremorYaw = 0.0f;
  float m_tremorPitch = 0.0f;
  float m_jitterYaw = 0.0f;
  float m_jitterPitch = 0.0f;

  float m_velYaw = 0.0f;
  float m_velPitch = 0.0f;
  float m_lastStepYaw = 0.0f;
  float m_lastStepPitch = 0.0f;
  float m_lastDelta = 0.0f;
  float m_lastFactor = 0.0f;
  float m_lastDt = 0.0f;
  bool m_deadzoneHit = false;

  mutable Vec3 m_eye{};
  std::minstd_rand m_rng{0x5EED1234u};
  std::chrono::steady_clock::time_point m_lastDebug{};
};

} // namespace aimbot
