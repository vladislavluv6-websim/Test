#include "ClientBridge.hpp"

#include <android/log.h>

#include "DiagLog.hpp"

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
// ClientInstance::update(this, bool, bool) — оба флага передаются в
// оригинал как есть, иначе третий аргумент уходит мусором.
using ClientInstanceUpdateFn = void *(*)(void *, bool, bool);
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

void *clientUpdateDetour(void *client, bool value, bool second) {
  static uint64_t s_updateCalls = 0;
  const uint64_t updateCall = ++s_updateCalls;
  if (updateCall <= 3 || (updateCall % 600) == 0) {
    aimbot::diagLog(kTag, "clientUpdate#%llu client=%p orig=%p",
                    static_cast<unsigned long long>(updateCall), client,
                    reinterpret_cast<void *>(g_clientUpdateOriginal));
  }

  void *result = g_clientUpdateOriginal
                     ? g_clientUpdateOriginal(client, value, second)
                     : nullptr;

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
  static uint64_t s_tickCalls = 0;
  const uint64_t tickCall = ++s_tickCalls;
  if (tickCall <= 3 || (tickCall % 600) == 0) {
    aimbot::diagLog(kTag, "normalTick#%llu actor=%p local=%p",
                    static_cast<unsigned long long>(tickCall), actor,
                    g_localPlayer.load(std::memory_order_relaxed));
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
    aimbot::diagLog(kTag,
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
      aimbot::diagLog(kTag,
                          "normalTick hook failed");
      return false;
    }
    if (pl::memory::hook(reinterpret_cast<pl::memory::FuncPtr>(clientUpdate),
                         reinterpret_cast<pl::memory::FuncPtr>(
                             &clientUpdateDetour),
                         reinterpret_cast<pl::memory::FuncPtr *>(
                             &g_clientUpdateOriginal)) != 0) {
      aimbot::diagLog(kTag,
                          "ClientInstance::update hook failed");
      return false;
    }
    g_hooksInstalled.store(true, std::memory_order_release);
  }

  g_initialized.store(true, std::memory_order_release);
  aimbot::diagLog(kTag,
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
  if (!client || !g_getLocalPlayer) return nullptr;
  void *player = g_getLocalPlayer(client);
  if (player) {
    g_localPlayer.store(player, std::memory_order_release);
  }
  return player;
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

  const ActorVec actors = g_fetchNearbyActors(player, &extent, actorType);
  const auto begin = reinterpret_cast<uintptr_t>(actors.begin);
  const auto end = reinterpret_cast<uintptr_t>(actors.end);
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

void *ClientBridge::getGameMode() const {
  void *client = getClientInstance();
  if (!client) return nullptr;

  uintptr_t gameMode = 0;
  if (!safeRead(reinterpret_cast<uintptr_t>(client) +
                    offsets::kClientInstanceGameMode,
                gameMode) ||
      !isPlausiblePointer(gameMode)) {
    return nullptr;
  }

  uintptr_t owner = 0;
  if (!safeRead(gameMode + offsets::kGameModeOwner, owner) ||
      owner != reinterpret_cast<uintptr_t>(client)) {
    return nullptr;
  }

  return reinterpret_cast<void *>(gameMode);
}

bool ClientBridge::attackActor(void *actor, const Vec3 &hitPoint) {
  if (!actor) return false;

  static std::atomic<int> s_attackLog{0};
  const int logIndex = s_attackLog.fetch_add(1, std::memory_order_relaxed);
  const bool logThis = logIndex < 8;

  void *gameMode = getGameMode();
  if (!gameMode) {
    if (logThis) {
      aimbot::diagLog(kTag,
                          "attack: gameMode=null client=%p", getClientInstance());
    }
    return false;
  }

  uintptr_t vtable = 0;
  if (!safeRead(reinterpret_cast<uintptr_t>(gameMode), vtable) ||
      !isPlausiblePointer(vtable)) {
    return false;
  }

  uintptr_t attackFn = 0;
  if (!safeRead(vtable + offsets::kGameModeAttackSlot * sizeof(uintptr_t),
                attackFn)) {
    return false;
  }
  if (!mem::isGameAddress(reinterpret_cast<const void *>(attackFn))) {
    if (logThis) {
      aimbot::diagLog(kTag,
                          "attack: fn=%p not in game (gm=%p vt=%p)",
                          reinterpret_cast<void *>(attackFn), gameMode,
                          reinterpret_cast<void *>(vtable));
    }
    return false;
  }

  if (logThis) {
    aimbot::diagLog(
        kTag,
        "attack #%d gm=%p fn=%p actor=%p hit=(%.2f,%.2f,%.2f)", logIndex,
        gameMode, reinterpret_cast<void *>(attackFn), actor,
        static_cast<double>(hitPoint.x), static_cast<double>(hitPoint.y),
        static_cast<double>(hitPoint.z));
  }

  // GameMode::attack(Actor&, Vec3 const&): слот сам выставляет actionType=1
  // и уходит в 4-арг реализацию, которая собирает и отправляет
  // ItemUseOnActorInventoryTransaction. hitPoint обязателен — по нему
  // считается точка удара, nullptr роняет колбэк транзакции.
  using AttackFn = void (*)(void *, void *, const Vec3 *);
  reinterpret_cast<AttackFn>(attackFn)(gameMode, actor, &hitPoint);
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
