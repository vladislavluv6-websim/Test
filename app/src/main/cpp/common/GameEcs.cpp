#include "GameEcs.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <sys/uio.h>
#include <unistd.h>

#include <entt/entt.hpp>

namespace gameecs {
namespace {

enum class EntityId : std::uint32_t {};

} // namespace
} // namespace gameecs

template <> struct entt::internal::entt_traits<gameecs::EntityId> {
  using value_type = gameecs::EntityId;
  using entity_type = std::uint32_t;
  using version_type = std::uint16_t;
  static constexpr entity_type entity_mask = 0x3FFFF;
  static constexpr entity_type version_mask = 0x3FFF;
};

namespace gameecs {
namespace {

using Registry = entt::basic_registry<EntityId>;
using SparseSet = Registry::common_type;

// entt component type hashes in the running Bedrock build.
constexpr entt::id_type kHashMoveInput = 0x468BCD2E;
constexpr entt::id_type kHashOnGround = 0xC3E299A3;
constexpr entt::id_type kHashEquipment = 0x71C44836;
constexpr entt::id_type kHashActorData = 0x33475976;

constexpr uint8_t kMoveFlagSneakDown = 0;
constexpr uint8_t kMoveFlagJumpDown = 7;
constexpr uint8_t kMoveFlagSprintDown = 8;

constexpr uint8_t kMoveStateSprinting = 1;
constexpr uint8_t kMoveStateJumping = 4;

constexpr uintptr_t kEquipmentArmorContainer = 0x08;
constexpr uintptr_t kContainerItems = 0x140;
constexpr uintptr_t kStackSize = 0x98;
constexpr uintptr_t kStackCount = 0x22;

bool readable(const void *address, size_t size) {
  if (!address || reinterpret_cast<uintptr_t>(address) < 0x1000) return false;
  iovec local{const_cast<void *>(address), size};
  iovec remote{const_cast<void *>(address), size};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) ==
         static_cast<ssize_t>(size);
}

bool readValue(uintptr_t address, void *out, size_t size) {
  if (!out || address < 0x1000) return false;
  iovec local{out, size};
  iovec remote{reinterpret_cast<void *>(address), size};
  return process_vm_readv(getpid(), &local, 1, &remote, 1, 0) ==
         static_cast<ssize_t>(size);
}

template <typename T> bool readValue(uintptr_t address, T &out) {
  return readValue(address, &out, sizeof(T));
}

SparseSet *poolFor(void *actor, entt::id_type hash, EntityId &entity) {
  void *registry = nullptr;
  uint32_t raw = 0;
  if (!registryForActor(actor, &registry, &raw)) return nullptr;
  if (!readable(registry, sizeof(void *) * 4)) return nullptr;
  auto *reg = static_cast<Registry *>(registry);
  SparseSet *pool = reg->storage(hash);
  if (!pool) return nullptr;
  entity = static_cast<EntityId>(raw);
  if (!pool->contains(entity)) return nullptr;
  return pool;
}

void *componentFor(void *actor, entt::id_type hash) {
  EntityId entity{};
  SparseSet *pool = poolFor(actor, hash, entity);
  if (!pool) return nullptr;
  return const_cast<void *>(pool->value(entity));
}

bool componentPresent(void *actor, entt::id_type hash) {
  EntityId entity{};
  return poolFor(actor, hash, entity) != nullptr;
}

void updateBitset32(void *component, uint8_t bit, bool value) {
  if (!component) return;
  const auto address = reinterpret_cast<uintptr_t>(component);
  uint32_t flags = 0;
  if (!readValue(address, flags)) return;
  const uint32_t mask = 1u << bit;
  flags = value ? (flags | mask) : (flags & ~mask);
  iovec local{&flags, sizeof(flags)};
  iovec remote{reinterpret_cast<void *>(address), sizeof(flags)};
  process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
}

void updateBitset16(void *component, uintptr_t offset, uint8_t bit,
                    bool value) {
  if (!component) return;
  const auto address = reinterpret_cast<uintptr_t>(component) + offset;
  uint16_t flags = 0;
  if (!readValue(address, flags)) return;
  const uint16_t mask = static_cast<uint16_t>(1u << bit);
  flags = value ? static_cast<uint16_t>(flags | mask)
                : static_cast<uint16_t>(flags & ~mask);
  iovec local{&flags, sizeof(flags)};
  iovec remote{reinterpret_cast<void *>(address), sizeof(flags)};
  process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
}

bool readBit32(void *component, uint8_t bit) {
  if (!component) return false;
  uint32_t flags = 0;
  if (!readValue(reinterpret_cast<uintptr_t>(component), flags)) return false;
  return (flags & (1u << bit)) != 0;
}

bool readBit64(void *component, uint8_t bit) {
  if (!component) return false;
  uint64_t flags = 0;
  if (!readValue(reinterpret_cast<uintptr_t>(component), flags)) return false;
  return (flags & (1ull << bit)) != 0;
}

} // namespace

bool registryForActor(void *actor, void **registry, uint32_t *entity) {
  if (!actor || !registry || !entity) return false;
  const auto base = reinterpret_cast<uintptr_t>(actor);
  uintptr_t contextRegistry = 0;
  uint32_t raw = 0;
  if (!readValue(base + kActorEntityContext + kEntityContextRegistry,
                 contextRegistry) ||
      !readValue(base + kActorEntityContext + kEntityContextEntity, raw)) {
    return false;
  }
  if (contextRegistry < 0x10000 || (contextRegistry & 0x7) != 0) return false;
  *registry = reinterpret_cast<void *>(contextRegistry);
  *entity = raw;
  return true;
}

bool hasOnGround(void *actor) {
  // OnGround is an empty tag component: presence is the value.
  return componentPresent(actor, kHashOnGround);
}

bool readMoveInput(void *actor, MoveInputState &out) {
  void *component = componentFor(actor, kHashMoveInput);
  if (!component) return false;
  out.sneaking = readBit32(component, kMoveFlagSneakDown);
  out.sprinting = readBit32(component, kMoveFlagSprintDown);
  out.jumping = readBit32(component, kMoveFlagJumpDown);
  return true;
}

bool setMoveSprinting(void *actor, bool value) {
  void *component = componentFor(actor, kHashMoveInput);
  if (!component) return false;
  updateBitset32(component, kMoveFlagSprintDown, value);
  updateBitset32(reinterpret_cast<void *>(
                     reinterpret_cast<uintptr_t>(component) + 0x10),
                 kMoveFlagSprintDown, value);
  updateBitset16(component, 0x60, kMoveStateSprinting, value);
  return true;
}

bool setMoveJumping(void *actor, bool value) {
  void *component = componentFor(actor, kHashMoveInput);
  if (!component) return false;
  updateBitset32(component, kMoveFlagJumpDown, value);
  updateBitset32(reinterpret_cast<void *>(
                     reinterpret_cast<uintptr_t>(component) + 0x10),
                 kMoveFlagJumpDown, value);
  updateBitset16(component, 0x60, kMoveStateJumping, value);
  return true;
}

int armorPieceCount(void *actor) {
  void *equipment = componentFor(actor, kHashEquipment);
  if (!equipment) return 0;
  const auto base = reinterpret_cast<uintptr_t>(equipment);
  uintptr_t container = 0;
  if (!readValue(base + kEquipmentArmorContainer, container) ||
      container < 0x1000) {
    return 0;
  }
  uintptr_t begin = 0;
  uintptr_t end = 0;
  if (!readValue(container + kContainerItems, begin) ||
      !readValue(container + kContainerItems + sizeof(void *), end)) {
    return 0;
  }
  if (begin < 0x1000 || end < begin) return 0;
  const size_t bytes = end - begin;
  if (bytes == 0 || bytes % kStackSize != 0) return 0;
  size_t slots = bytes / kStackSize;
  if (slots > 4) slots = 4;
  int pieces = 0;
  for (size_t slot = 0; slot < slots; ++slot) {
    // FillingContainer stores ItemStack objects inline; the item data starts
    // at the stack address itself (see BedrockTools armorhud.cpp).
    const uintptr_t stack = begin + slot * kStackSize;
    uint8_t count = 0;
    uint8_t valid = 0;
    if ((readValue(stack + kStackCount, count) && count > 0) ||
        (readValue(stack + kStackCount + 1, valid) && valid != 0)) {
      ++pieces;
    }
  }
  return pieces;
}

bool hasArmor(void *actor) { return armorPieceCount(actor) > 0; }

bool isInvisible(void *actor) {
  return readBit64(componentFor(actor, kHashActorData), 5);
}

bool setSprintingFlag(void *actor, bool value) {
  void *data = componentFor(actor, kHashActorData);
  if (!data) return false;
  const auto address = reinterpret_cast<uintptr_t>(data);
  uint64_t flags = 0;
  if (!readValue(address, flags)) return false;
  constexpr uint64_t kSprintingBit = 1ull << 3;
  flags = value ? (flags | kSprintingBit) : (flags & ~kSprintingBit);
  iovec local{&flags, sizeof(flags)};
  iovec remote{reinterpret_cast<void *>(address), sizeof(flags)};
  return process_vm_writev(getpid(), &local, 1, &remote, 1, 0) ==
         static_cast<ssize_t>(sizeof(flags));
}

} // namespace gameecs
