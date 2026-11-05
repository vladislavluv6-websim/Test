#include "ClientBridge.hpp"

#include <android/log.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <type_traits>

#include "AimAssist.hpp"
#include "Offsets.hpp"
#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

namespace aimbot {
namespace {

constexpr const char *kTag = "AimAssistBridge";

struct DistanceSortedActor {
  void *actor = nullptr;
  float distance = 0.0f;
  float pad = 0.0f;
};

struct ActorVec {
  DistanceSortedActor *begin = nullptr;
  DistanceSortedActor *end = nullptr;
  DistanceSortedActor *cap = nullptr;
};

struct Rotation {
  float pitch = 0.0f;
  float yaw = 0.0f;
};

using NormalTickFn = void (*)(void *);
using ClientInstanceUpdateFn = void *(*)(void *, bool);
using GetLocalPlayerFn = void *(*)(void *);
using ApplyTurnDeltaFn = void (*)(void *, const Rotation &);
using GetHeadRotationFn = Rotation (*)(void *);
using FetchNearbyActorsFn = ActorVec (*)(void *, const Vec3 *, int);
using ActorIsPlayerFn = bool (*)(void *);

NormalTickFn g_normalTickOriginal = nullptr;
ClientInstanceUpdateFn g_clientUpdateOriginal = nullptr;
GetLocalPlayerFn g_getLocalPlayer = nullptr;
ApplyTurnDeltaFn g_applyTurnDelta = nullptr;
GetHeadRotationFn g_getHeadRotation = nullptr;
FetchNearbyActorsFn g_fetchNearbyActors = nullptr;
ActorIsPlayerFn g_actorIsPlayer = nullptr;

std::atomic<void *> g_clientInstance{nullptr};
std::atomic<void *> g_localPlayer{nullptr};
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_hooksInstalled{false};
std::mutex g_initMutex;

std::chrono::steady_clock::time_point g_lastFrame{};
std::atomic<ClientBridge::MovePacketSender> g_movePacketSender{nullptr};

bool isPlausiblePointer(uintptr_t address) {
  return address >= 0x1000 && (address & 0x3) == 0;
}

// Android не даёт MSVC-совместимый __try/__except.
// Здесь проверяются диапазон/выравнивание, а для полностью безопасного чтения
// подключи safe-read из SDK клиента (если он есть).
template <typename T>
bool safeRead(uintptr_t address, T &out) {
  static_assert(std::is_trivially_copyable<T>::value,
                "safeRead requires a trivial type");
  if (!isPlausiblePointer(address)) {
    return false;
  }
  std::memcpy(&out, reinterpret_cast<const void *>(address), sizeof(T));
  return true;
}

template <typename T>
bool safeWrite(uintptr_t address, const T &value) {
  static_assert(std::is_trivially_copyable<T>::value,
                "safeWrite requires a trivial type");
  if (!isPlausiblePointer(address)) {
    return false;
  }
  std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(T));
  return true;
}

bool readRotationComponent(void *actor, Rotation &out) {
  if (!actor) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);
  uintptr_t component = 0;
  if (!safeRead(base + offsets::kActorRotationComponent, component) ||
      !isPlausiblePointer(component)) {
    return false;
  }
  return safeRead(component, out);
}

bool writeRotationComponent(void *actor, const Rotation &value) {
  if (!actor) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);
  uintptr_t component = 0;
  if (!safeRead(base + offsets::kActorRotationComponent, component) ||
      !isPlausiblePointer(component)) {
    return false;
  }
  return safeWrite(component, value);
}

float frameDelta() {
  const auto now = std::chrono::steady_clock::now();
  if (g_lastFrame.time_since_epoch().count() == 0) {
    g_lastFrame = now;
    return 1.0f / 60.0f;
  }
  const float dt =
      std::chrono::duration<float>(now - g_lastFrame).count();
  g_lastFrame = now;
  return std::clamp(dt, 0.0f, 0.05f);
}

void *clientUpdateDetour(void *client, bool value) {
  void *result =
      g_clientUpdateOriginal ? g_clientUpdateOriginal(client, value) : nullptr;

  if (client) {
    g_clientInstance.store(client, std::memory_order_relaxed);
    if (g_getLocalPlayer) {
      void *localPlayer = g_getLocalPlayer(client);
      if (localPlayer) {
        g_localPlayer.store(localPlayer, std::memory_order_relaxed);
      }
    }
    // ClientInstance::update вызывается в кадровом цикле.
    AimAssist::instance().onRender(frameDelta());
  }
  return result;
}

void normalTickDetour(void *actor) {
  if (g_normalTickOriginal) {
    g_normalTickOriginal(actor);
  }
  // Тик локального игрока — 20 Гц. Сетевую синхронизацию делаем только здесь.
  if (actor && actor == g_localPlayer.load(std::memory_order_relaxed)) {
    AimAssist::instance().onTick();
  }
}

} // namespace

ClientBridge &ClientBridge::instance() {
  static ClientBridge bridge;
  return bridge;
}

bool ClientBridge::initialize() {
  std::lock_guard<std::mutex> lock(g_initMutex);
  if (g_initialized.load(std::memory_order_acquire)) {
    return true;
  }

  const uintptr_t normalTick =
      pl::memory::resolveSignature(offsets::kNormalTickSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t clientUpdate =
      pl::memory::resolveSignature(offsets::kClientInstanceUpdateSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t getLocalPlayer =
      pl::memory::resolveSignature(
          offsets::kClientInstanceGetLocalPlayerSignature,
          offsets::kMinecraftLibrary);
  const uintptr_t applyTurnDelta =
      pl::memory::resolveSignature(offsets::kApplyTurnDeltaSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t getHeadRotation =
      pl::memory::resolveSignature(offsets::kActorGetHeadRotationSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t fetchNearbyActors =
      pl::memory::resolveSignature(offsets::kFetchNearbyActorsSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t actorIsPlayer =
      pl::memory::resolveSignature(offsets::kActorIsPlayerSignature,
                                   offsets::kMinecraftLibrary);

  if (!normalTick || !clientUpdate || !getLocalPlayer || !applyTurnDelta ||
      !fetchNearbyActors || !actorIsPlayer) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "signature resolve failed: tick=%p update=%p "
                        "local=%p delta=%p nearby=%p isPlayer=%p",
                        reinterpret_cast<void *>(normalTick),
                        reinterpret_cast<void *>(clientUpdate),
                        reinterpret_cast<void *>(getLocalPlayer),
                        reinterpret_cast<void *>(applyTurnDelta),
                        reinterpret_cast<void *>(fetchNearbyActors),
                        reinterpret_cast<void *>(actorIsPlayer));
    return false;
  }

  g_getLocalPlayer = reinterpret_cast<GetLocalPlayerFn>(getLocalPlayer);
  g_applyTurnDelta = reinterpret_cast<ApplyTurnDeltaFn>(applyTurnDelta);
  g_getHeadRotation = reinterpret_cast<GetHeadRotationFn>(getHeadRotation);
  g_fetchNearbyActors =
      reinterpret_cast<FetchNearbyActorsFn>(fetchNearbyActors);
  g_actorIsPlayer = reinterpret_cast<ActorIsPlayerFn>(actorIsPlayer);

  if (!g_hooksInstalled.load(std::memory_order_acquire)) {
    if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(normalTick),
                         reinterpret_cast<pl::memory::FuncPtr>(&normalTickDetour),
                         reinterpret_cast<pl::memory::FuncPtr *>(
                             &g_normalTickOriginal)) != 0) {
      __android_log_print(ANDROID_LOG_WARN, kTag,
                          "normalTick hook failed");
      return false;
    }
    if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(clientUpdate),
                         reinterpret_cast<pl::memory::FuncPtr>(
                             &clientUpdateDetour),
                         reinterpret_cast<pl::memory::FuncPtr *>(
                             &g_clientUpdateOriginal)) != 0) {
      __android_log_print(ANDROID_LOG_WARN, kTag,
                          "ClientInstance::update hook failed");
      return false;
    }
    g_hooksInstalled.store(true, std::memory_order_release);
  }

  g_initialized.store(true, std::memory_order_release);
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "bridge initialized: tick=%p update=%p delta=%p "
                      "headGet=%p",
                      reinterpret_cast<void *>(normalTick),
                      reinterpret_cast<void *>(clientUpdate),
                      reinterpret_cast<void *>(applyTurnDelta),
                      reinterpret_cast<void *>(getHeadRotation));
  return true;
}

void ClientBridge::shutdown() {
  g_initialized.store(false, std::memory_order_release);
  g_localPlayer.store(nullptr, std::memory_order_release);
  g_clientInstance.store(nullptr, std::memory_order_release);
}

void ClientBridge::setClientInstance(void *client) {
  g_clientInstance.store(client, std::memory_order_release);
}

void *ClientBridge::getClientInstance() const {
  return g_clientInstance.load(std::memory_order_acquire);
}

void *ClientBridge::getLocalPlayer() const {
  void *cached = g_localPlayer.load(std::memory_order_acquire);
  if (cached) return cached;
  void *client = g_clientInstance.load(std::memory_order_acquire);
  if (!client || !g_getLocalPlayer) return nullptr;
  void *player = g_getLocalPlayer(client);
  if (player) {
    g_localPlayer.store(player, std::memory_order_release);
  }
  return player;
}

bool ClientBridge::getViewAngles(void *player, ViewAngles &out) const {
  if (!player) return false;

  // Для локального игрока head rotation — это реальный угол камеры.
  // Rotation-компонент ниже остаётся body/legacy fallback.
  if (g_getHeadRotation) {
    const Rotation head = g_getHeadRotation(player);
    if (std::isfinite(head.pitch) && std::isfinite(head.yaw)) {
      out.pitch = head.pitch;
      out.yaw = head.yaw;
      return true;
    }
  }

  Rotation rotation{};
  if (!readRotationComponent(player, rotation)) {
    return false;
  }
  out.pitch = rotation.pitch;
  out.yaw = rotation.yaw;
  return true;
}

bool ClientBridge::applyTurnDelta(void *player, float pitchUp, float yaw) {
  if (!player || !g_applyTurnDelta) return false;
  const Rotation delta{pitchUp, yaw};
  g_applyTurnDelta(player, delta);
  return true;
}

bool ClientBridge::setBodyYaw(void *player, float yaw) {
  Rotation rotation{};
  if (!readRotationComponent(player, rotation)) {
    return false;
  }
  rotation.yaw = yaw;
  return writeRotationComponent(player, rotation);
}

bool ClientBridge::getPosition(void *actor, Vec3 &out) const {
  if (!actor) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);
  uintptr_t stateVector = 0;
  if (!safeRead(base + offsets::kActorStateVectorComponent, stateVector) ||
      !isPlausiblePointer(stateVector)) {
    return false;
  }
  float position[3]{};
  if (!safeRead(stateVector + offsets::kStatePosition, position)) {
    return false;
  }
  if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2])) {
    return false;
  }
  out.x = position[0];
  out.y = position[1];
  out.z = position[2];
  return true;
}

bool ClientBridge::getHitboxCenter(void *actor, Vec3 &out) const {
  if (!actor) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);

  uintptr_t stateVector = 0;
  if (!safeRead(base + offsets::kActorStateVectorComponent, stateVector) ||
      !isPlausiblePointer(stateVector)) {
    return false;
  }

  // AABBShapeComponent лежит рядом с StateVectorComponent в built-in
  // component storage; внутри компонента +0 — сам AABB.
  uintptr_t aabbComponent = 0;
  if (!safeRead(base + offsets::kActorStateVectorComponent +
                    offsets::kActorAabbShapeComponent,
                aabbComponent) ||
      !isPlausiblePointer(aabbComponent)) {
    return false;
  }

  Vec3 min{};
  Vec3 max{};
  if (!safeRead(aabbComponent + offsets::kAabbMin, min) ||
      !safeRead(aabbComponent + offsets::kAabbMax, max)) {
    return false;
  }
  if (!std::isfinite(min.x) || !std::isfinite(min.y) || !std::isfinite(min.z) ||
      !std::isfinite(max.x) || !std::isfinite(max.y) || !std::isfinite(max.z)) {
    return false;
  }
  if (max.x <= min.x || max.y <= min.y || max.z <= min.z) return false;

  // Отсекаем мусор от неверного оффсета: хитбокс не может быть
  // гигантским и его центр должен быть рядом с позицией актёра.
  if (max.x - min.x > 4.0f || max.y - min.y > 8.0f ||
      max.z - min.z > 4.0f) {
    return false;
  }

  Vec3 position{};
  if (!getPosition(actor, position)) return false;
  const float centerX = (min.x + max.x) * 0.5f;
  const float centerY = (min.y + max.y) * 0.5f;
  const float centerZ = (min.z + max.z) * 0.5f;
  const float dx = centerX - position.x;
  const float dy = centerY - position.y;
  const float dz = centerZ - position.z;
  if (dx * dx + dy * dy + dz * dz > 16.0f) return false;

  out.x = centerX;
  out.y = centerY;
  out.z = centerZ;
  return true;
}

bool ClientBridge::isPlayer(void *actor) const {
  if (!actor) return false;
  return g_actorIsPlayer ? g_actorIsPlayer(actor) : true;
}

int ClientBridge::getActorType(void *) const {
  // В этой версии тип не нужен: fetchNearbyActors уже отдаёт mob-список,
  // а isPlayer отсекает не-игроков.
  return offsets::kActorTypeMob;
}

uint64_t ClientBridge::getActorFlags(void *) const {
  return 0;
}

void ClientBridge::fillNearbyActors(void *player, float range,
                                    std::vector<void *> &out) const {
  out.clear();
  if (!player || !g_fetchNearbyActors) return;

  const Vec3 extent{range, range, range};
  const ActorVec actors = g_fetchNearbyActors(player, &extent,
                                              offsets::kActorTypeMob);
  if (!actors.begin || !actors.end || actors.end <= actors.begin) {
    return;
  }

  size_t count = static_cast<size_t>(actors.end - actors.begin);
  if (count > 512) count = 512;
  out.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    void *actor = actors.begin[i].actor;
    if (actor) out.push_back(actor);
  }
}

bool ClientBridge::hasLineOfSight(void *player, const Vec3 &, const Vec3 &) const {
  // Сигнатура Level::raycast зависит от версии. Пока не заполнишь её —
  // visibleCheck работает как no-op, чтобы не ломать наведение.
  (void)player;
  return true;
}

void ClientBridge::setMovePacketSender(MovePacketSender sender) {
  g_movePacketSender.store(sender, std::memory_order_release);
}

void ClientBridge::sendMovePacket(void *player, float yaw, float pitch) const {
  MovePacketSender sender = g_movePacketSender.load(std::memory_order_acquire);
  if (sender) {
    sender(player, yaw, pitch);
  }
}

} // namespace aimbot
