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
#include "Memory.hpp"
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
using FetchNearbyActorsFn = ActorVec (*)(void *, const Vec3 *, int);
using ActorIsPlayerFn = bool (*)(void *);

NormalTickFn g_normalTickOriginal = nullptr;
ClientInstanceUpdateFn g_clientUpdateOriginal = nullptr;
GetLocalPlayerFn g_getLocalPlayer = nullptr;
ApplyTurnDeltaFn g_applyTurnDelta = nullptr;
FetchNearbyActorsFn g_fetchNearbyActors = nullptr;
ActorIsPlayerFn g_actorIsPlayer = nullptr;

std::atomic<void *> g_clientInstance{nullptr};
std::atomic<void *> g_localPlayer{nullptr};
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_hooksInstalled{false};
std::mutex g_initMutex;

std::chrono::steady_clock::time_point g_lastFrame{};
std::atomic<ClientBridge::MovePacketSender> g_movePacketSender{nullptr};
int64_t g_lastNearbyLogMs = 0;
int64_t g_lastLocalPlayerScanMs = 0;
bool g_loggedBadLocalPlayer = false;

bool isPlausiblePointer(uintptr_t address) {
  return mem::isPlausiblePointer(address);
}

// process_vm_readv проверяет, что страница реально отображена. Это важно:
// просто memcpy по «правдоподобному» указателю из компонента актёра роняет игру,
// если клиент успел пересоздать актёра или оффсет не совпал с версией.
template <typename T>
bool safeRead(uintptr_t address, T &out) {
  return mem::read(address, out);
}

template <typename T>
bool safeWrite(uintptr_t address, const T &value) {
  return mem::write(address, value);
}

bool readRotationComponent(void *actor, Rotation &out) {
  if (!actor) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);
  uintptr_t component = 0;
  if (!safeRead(base + offsets::kActorRotationComponent, component) ||
      !isPlausiblePointer(component)) {
    return false;
  }
  if (!safeRead(component, out)) return false;
  if (!std::isfinite(out.pitch) || !std::isfinite(out.yaw)) return false;
  return true;
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
    void *previousClient =
        g_clientInstance.exchange(client, std::memory_order_acq_rel);
    // Сменился ClientInstance (новый мир / перезапуск сессии) — кеш
    // локального игрока больше не валиден.
    if (previousClient != client) {
      g_localPlayer.store(nullptr, std::memory_order_release);
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
  const uintptr_t fetchNearbyActors =
      pl::memory::resolveSignature(offsets::kFetchNearbyActorsSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t actorIsPlayer =
      pl::memory::resolveSignature(offsets::kActorIsPlayerSignature,
                                   offsets::kMinecraftLibrary);

  const auto validGameFn = [](uintptr_t address) {
    return address != 0 && mem::isGameAddress(
                                   reinterpret_cast<const void *>(address));
  };
  if (!validGameFn(normalTick) || !validGameFn(clientUpdate) ||
      !validGameFn(getLocalPlayer) || !validGameFn(applyTurnDelta) ||
      !validGameFn(fetchNearbyActors) || !validGameFn(actorIsPlayer)) {
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
                      "bridge initialized: tick=%p update=%p delta=%p",
                      reinterpret_cast<void *>(normalTick),
                      reinterpret_cast<void *>(clientUpdate),
                      reinterpret_cast<void *>(applyTurnDelta));
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
  if (!client) return nullptr;

  // 1) Основной путь: сигнатура ClientInstance::getLocalPlayer.
  if (g_getLocalPlayer) {
    void *player = g_getLocalPlayer(client);
    if (player) {
      uintptr_t vtable = 0;
      if (safeRead(reinterpret_cast<uintptr_t>(player), vtable) &&
          mem::isGameAddress(reinterpret_cast<const void *>(vtable))) {
        g_localPlayer.store(player, std::memory_order_release);
        return player;
      }
      if (!g_loggedBadLocalPlayer) {
        g_loggedBadLocalPlayer = true;
        __android_log_print(ANDROID_LOG_WARN, kTag,
                            "getLocalPlayer signature returned non-actor %p, "
                            "falling back to ClientInstance scan",
                            player);
      }
    }
  }

  // Сканирование ClientInstance дорогое: не чаще раза в 500 мс.
  const int64_t scanNow =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  if (scanNow - g_lastLocalPlayerScanMs < 500) return nullptr;
  g_lastLocalPlayerScanMs = scanNow;

  // 2) Резерв: LocalPlayer лежит полем прямо в ClientInstance. Ищем
  // указатель на Actor, у которого Actor::mLevel (+0x28) валиден и у
  // объекта уровня на +0x230 лежит правдоподобная позиция (Vec3).
  // ClientInstance хранит только локального игрока, поэтому результат
  // однозначен.
  for (uintptr_t off = 0x8; off <= offsets::kClientInstanceLevelScanLimit;
       off += sizeof(uintptr_t)) {
    uintptr_t candidate = 0;
    if (!safeRead(reinterpret_cast<uintptr_t>(client) + off, candidate) ||
        !isPlausiblePointer(candidate)) {
      continue;
    }
    void *actor = reinterpret_cast<void *>(candidate);
    uintptr_t level = 0;
    if (!safeRead(candidate + offsets::kActorLevel, level) ||
        !isPlausiblePointer(level)) {
      continue;
    }
    // Level+0x230 — указатель на AABB/границы измерения (6 float:
    // minX,minY,minZ,maxX,maxY,maxZ), а не сам Vec3.
    uintptr_t levelBounds = 0;
    if (!safeRead(level + offsets::kLevelBounds, levelBounds) ||
        !isPlausiblePointer(levelBounds)) {
      continue;
    }
    float bounds[6]{};
    if (!safeRead(levelBounds, bounds)) continue;
    bool finite = true;
    for (float value : bounds) {
      if (!std::isfinite(value) || std::fabs(value) > 3.0e7f) {
        finite = false;
        break;
      }
    }
    if (!finite) continue;
    if (g_actorIsPlayer && !g_actorIsPlayer(actor)) continue;
    Rotation playerRotation{};
    if (!readRotationComponent(actor, playerRotation)) continue;

    g_localPlayer.store(actor, std::memory_order_release);
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "local player resolved by scan: client+0x%zx -> %p "
                        "(level=%p)",
                        static_cast<size_t>(off), actor,
                        reinterpret_cast<void *>(level));
    return actor;
  }
  return nullptr;
}

void *ClientBridge::getLevel(void *actor) const {
  if (!actor) return nullptr;
  uintptr_t level = 0;
  if (!safeRead(reinterpret_cast<uintptr_t>(actor) + offsets::kActorLevel,
                level) ||
      !isPlausiblePointer(level)) {
    return nullptr;
  }
  return reinterpret_cast<void *>(level);
}

bool ClientBridge::getViewAngles(void *player, ViewAngles &out) const {
  if (!player) return false;

  // ActorRotationComponent — это фактический view камеры. applyTurnDelta
  // меняет именно его, поэтому не смешиваем его с отдельным head getter.
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
  if (!std::isfinite(pitchUp) || !std::isfinite(yaw)) return false;
  if (std::fabs(pitchUp) > 90.0f || std::fabs(yaw) > 180.0f) return false;
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

void ClientBridge::appendNearbyActors(void *player, const Vec3 &extent,
                                      int actorType,
                                      std::vector<void *> &out) const {
  if (!player || !g_fetchNearbyActors) return;

  // Level::fetchNearbyActors принимает Level* первым аргументом (x0) —
  // не Actor*. Снято с 1.26.60.04 RC3: перед вызовом код делает
  // `ldr x0, [actor, #0x28]`, а `w2` — маска типа (0x100 / 0x40 / -1).
  void *level = getLevel(player);
  if (!level) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "fetchNearbyActors: no Level for player %p", player);
    return;
  }

  const ActorVec actors =
      g_fetchNearbyActors(level, &extent, actorType);
  const auto begin = reinterpret_cast<uintptr_t>(actors.begin);
  const auto end = reinterpret_cast<uintptr_t>(actors.end);
  const int64_t diagNow =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  if (diagNow - g_lastNearbyLogMs >= 1000) {
    g_lastNearbyLogMs = diagNow;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "fetchNearbyActors(type=%d extent=%.1f) -> "
                        "begin=%p end=%p cap=%p",
                        actorType, extent.x,
                        static_cast<void *>(actors.begin),
                        static_cast<void *>(actors.end),
                        static_cast<void *>(actors.cap));
  }
  if (!isPlausiblePointer(begin) || !isPlausiblePointer(end) || end <= begin) {
    return;
  }

  size_t count = static_cast<size_t>(end - begin) /
                 sizeof(DistanceSortedActor);
  if (count == 0) return;
  if (count > 512) count = 512;
  if (out.capacity() < out.size() + count) {
    out.reserve(out.size() + count);
  }
  for (size_t i = 0; i < count; ++i) {
    DistanceSortedActor entry{};
    if (!safeRead(begin + i * sizeof(DistanceSortedActor), entry)) break;
    if (!entry.actor) continue;
    // Не отдаём aim assist указатель, у которого уже нет живого
    // StateVectorComponent: дальнейшие вызовы isPlayer/getPosition по нему
    // и раньше приводили к падению после респавна/смены измерения.
    uintptr_t stateVector = 0;
    if (!safeRead(reinterpret_cast<uintptr_t>(entry.actor) +
                      offsets::kActorStateVectorComponent,
                  stateVector) ||
        !isPlausiblePointer(stateVector)) {
      continue;
    }
    // Один и тот же актёр может прийти из двух наборов (mob + all).
    bool duplicate = false;
    for (void *existing : out) {
      if (existing == entry.actor) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      out.push_back(entry.actor);
    }
  }
}

void ClientBridge::fillNearbyActors(void *player, float range,
                                    std::vector<void *> &out) const {
  out.clear();
  if (!player || !g_fetchNearbyActors) return;

  const Vec3 extent{range, range, range};
  appendNearbyActors(player, extent, offsets::kActorTypeMob, out);

  // На части сборок mob-маска не содержит игроков. Если игроков в списке
  // нет — запрашиваем полный набор; дубликаты отсекаются в appendNearbyActors.
  bool hasPlayer = false;
  for (void *actor : out) {
    if (isPlayer(actor)) {
      hasPlayer = true;
      break;
    }
  }
  if (!hasPlayer) {
    appendNearbyActors(player, extent, offsets::kActorTypeAll, out);
  }
}

bool ClientBridge::getActorAabb(void *actor, Aabb &out) const {
  if (!actor) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);
  uintptr_t aabbComponent = 0;
  if (!safeRead(base + offsets::kActorAabbComponent, aabbComponent) ||
      !isPlausiblePointer(aabbComponent)) {
    return false;
  }
  if (!safeRead(aabbComponent, out)) return false;
  if (!std::isfinite(out.min.x) || !std::isfinite(out.min.y) ||
      !std::isfinite(out.min.z) || !std::isfinite(out.max.x) ||
      !std::isfinite(out.max.y) || !std::isfinite(out.max.z)) {
    return false;
  }
  return out.max.x > out.min.x && out.max.y > out.min.y &&
         out.max.z > out.min.z;
}

bool ClientBridge::getActorBounds(void *actor, Aabb &out) const {
  Vec3 position{};
  if (!getPosition(actor, position)) return false;

  Aabb aabb{};
  if (getActorAabb(actor, aabb)) {
    const float height = aabb.max.y - aabb.min.y;
    const float width = std::max(aabb.max.x - aabb.min.x, aabb.max.z - aabb.min.z);
    const float cx = (aabb.min.x + aabb.max.x) * 0.5f;
    const float cy = (aabb.min.y + aabb.max.y) * 0.5f;
    const float cz = (aabb.min.z + aabb.max.z) * 0.5f;
    const float dx = cx - position.x;
    const float dy = cy - position.y;
    const float dz = cz - position.z;
    // Хитбокс должен быть рядом с позицией актёра и иметь разумный размер.
    if (height >= 0.1f && height <= 8.0f && width >= 0.05f && width <= 8.0f &&
        dx * dx + dy * dy + dz * dz <= 16.0f) {
      out = aabb;
      return true;
    }
  }

  // Fallback: у игроков StateVector-позиция находится на уровне глаз,
  // у мобов — на уровне ног.
  const bool player = isPlayer(actor);
  const float feetY = player ? position.y - offsets::kEyeHeight : position.y;
  const float halfWidth = 0.3f;
  const float height = player ? 1.8f : 1.0f;
  out.min = {position.x - halfWidth, feetY, position.z - halfWidth};
  out.max = {position.x + halfWidth, feetY + height, position.z + halfWidth};
  return true;
}

bool ClientBridge::getEyePosition(void *player, Vec3 &out) const {
  Aabb bounds{};
  if (!getActorBounds(player, bounds)) return false;
  const float height = bounds.max.y - bounds.min.y;
  out.x = (bounds.min.x + bounds.max.x) * 0.5f;
  out.z = (bounds.min.z + bounds.max.z) * 0.5f;
  out.y = bounds.min.y + std::min(offsets::kEyeHeight, height * 0.9f);
  return true;
}

uint32_t ClientBridge::getActorCategories(void *actor) const {
  if (!actor) return 0;
  uint32_t categories = 0;
  if (!safeRead(reinterpret_cast<uintptr_t>(actor) + offsets::kActorCategories,
                categories)) {
    return 0;
  }
  return categories;
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
