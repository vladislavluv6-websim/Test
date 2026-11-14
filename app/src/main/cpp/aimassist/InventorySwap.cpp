#include "InventorySwap.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

#include "ClientBridge.hpp"
#include "DiagLog.hpp"
#include "GameEcs.hpp"
#include "Memory.hpp"

namespace aimbot {
namespace {

constexpr uintptr_t kPlayerInventory = 0x590;
constexpr uintptr_t kPlayerInventoryContainer = 0xB8;
constexpr uintptr_t kContainerItems = 0x140;
constexpr size_t kItemStackSize = 0x98;
constexpr size_t kMaxSlots = 36;
constexpr size_t kGetEquippedTotemVtableIndex = 80;
constexpr size_t kSetOffhandSlotVtableIndex = 79;
constexpr size_t kSetItemVtableIndex = 13;
constexpr size_t kItemGetDescriptionIdVtableIndex = 5;

const char *const kTag = "AutoSwap";
const char *const kTargetIdentifier = "minecraft:totem_of_undying";

using ActorOffhandFn = void (*)(void *, const void *);
using ActorGetOffhandFn = const void *(*)(void *);
using ContainerSetItemFn = void (*)(void *, int, const void *);

enum class Pending : int { None = 0, On = 1, Off = 2 };
std::atomic<int> g_action{static_cast<int>(Pending::None)};

// lastResult: 0=нет результата, 1=totem в левой руке, 2=он вернулся;
// -1=нет предмета, -2=не прочитан инвентарь, -3=нет LocalPlayer.
std::atomic<int> g_lastResult{0};
// 0=выключено, 1=ожидает применения, 2=включено (totem в offhand),
// -1/-2/-3 == значения g_lastResult при последней ошибке.
std::atomic<int> g_status{0};
std::atomic<int> g_originalSlot{-1};
std::atomic<bool> g_armed{false};

bool readPointer(uintptr_t address, uintptr_t &value) {
  if (!aimbot::mem::read(address, value)) return false;
  return aimbot::mem::isPlausiblePointer(value);
}

uintptr_t virtualFunction(void *object, size_t index) {
  uintptr_t vtable = 0;
  if (!object || !aimbot::mem::read(reinterpret_cast<uintptr_t>(object), vtable) ||
      !aimbot::mem::isGameAddress(reinterpret_cast<void *>(vtable))) {
    return 0;
  }
  uintptr_t fn = 0;
  if (!aimbot::mem::read(vtable + index * sizeof(uintptr_t), fn)) return 0;
  return aimbot::mem::isGameAddress(reinterpret_cast<void *>(fn)) ? fn : 0;
}

void *playerInventory(void *player) {
  uintptr_t proxy = 0;
  uintptr_t container = 0;
  if (!readPointer(reinterpret_cast<uintptr_t>(player) + kPlayerInventory,
                   proxy) ||
      !readPointer(proxy + kPlayerInventoryContainer, container)) {
    return nullptr;
  }
  // Важно: проверяем vtable *внутри* контейнера. Сам контейнер живёт
  // в куче, а vtable его конкретного класса — в libminecraftpe.so.
  return virtualFunction(reinterpret_cast<void *>(container), 8)
             ? reinterpret_cast<void *>(container)
             : nullptr;
}

bool inventoryItems(void *inventory, uintptr_t &begin, uintptr_t &end) {
  if (!inventory) return false;
  if (!readPointer(reinterpret_cast<uintptr_t>(inventory) + kContainerItems,
                   begin) ||
      !readPointer(reinterpret_cast<uintptr_t>(inventory) + kContainerItems +
                       sizeof(uintptr_t),
                   end)) {
    return false;
  }
  if (end < begin || (end - begin) % kItemStackSize != 0) return false;
  const size_t count = (end - begin) / kItemStackSize;
  // В некоторых состояниях список содержит больше фиксированных 36: в релизе
  // BedrockTools читают до 64 slots, а не режут проверку по size==36.
  return count >= kMaxSlots && count <= 64;
}

void *itemAt(void *inventory, int slot) {
  uintptr_t begin = 0;
  uintptr_t end = 0;
  if (slot < 0 || slot >= static_cast<int>(kMaxSlots) ||
      !inventoryItems(inventory, begin, end)) {
    return nullptr;
  }
  return reinterpret_cast<void *>(begin + static_cast<uintptr_t>(slot) *
                                            kItemStackSize);
}

const void *offhandItem(void *player) {
  const uintptr_t fn = virtualFunction(player, kGetEquippedTotemVtableIndex);
  return fn ? reinterpret_cast<ActorGetOffhandFn>(fn)(player) : nullptr;
}

bool equipOffhand(void *player, const void *item) {
  const uintptr_t fn =
      virtualFunction(player, kSetOffhandSlotVtableIndex);
  if (!fn || !item) return false;
  reinterpret_cast<ActorOffhandFn>(fn)(player, item);
  return true;
}

bool setInventorySlot(void *inventory, int slot, const void *item) {
  const uintptr_t fn = virtualFunction(inventory, kSetItemVtableIndex);
  if (!fn || !item) return false;
  reinterpret_cast<ContainerSetItemFn>(fn)(inventory, slot, item);
  return true;
}

void *itemObject(void *stack) {
  if (!stack) return nullptr;
  uintptr_t counter = 0;
  uintptr_t item = 0;
  if (!readPointer(reinterpret_cast<uintptr_t>(stack) + 0x8, counter) ||
      !aimbot::mem::read(counter, item)) {
    return nullptr;
  }
  return reinterpret_cast<void *>(item);
}

std::string itemIdentifier(void *stack) {
  std::string identifier;
  if (void *item = itemObject(stack)) {
    const uintptr_t fn = virtualFunction(item, kItemGetDescriptionIdVtableIndex);
    if (fn) {
      const auto &value =
          *reinterpret_cast<const std::string *(*)(void *)>(fn)(item);
      if (value.size() < 256) identifier = value;
    }
  }
  // На части внутренних ItemStack vtable counter совпадает с основным
  // инвентарём, поэтому сначала пытаемся getDescriptionId, а пустое или
  // неполное имя дополняем уже проверенным читателем стека из ESP.
  if (identifier.empty()) {
    identifier = gameecs::stackItemName(reinterpret_cast<uintptr_t>(stack));
  }
  return identifier;
}

bool isTotem(void *stack) {
  const std::string identifier = itemIdentifier(stack);
  return identifier == kTargetIdentifier ||
         identifier.find("totem_of_undying") != std::string::npos;
}

void dumpInventory(void *player, void *inventory) {
  uintptr_t begin = 0;
  uintptr_t end = 0;
  if (!inventory) {
    diagLog(kTag, "inventory read failed: player=%p", player);
    return;
  }
  if (!inventoryItems(inventory, begin, end)) {
    diagLog(kTag, "invalid container begin=%p end=%p (player=%p inventory=%p)",
            reinterpret_cast<void *>(begin), reinterpret_cast<void *>(end),
            player, inventory);
    return;
  }
  const int count = static_cast<int>((end - begin) / kItemStackSize);
  diagLog(kTag, "inventory slots=%d", count);
  for (int slot = 0; slot < count && slot < 64; ++slot) {
    const std::string name = itemIdentifier(
        reinterpret_cast<void *>(begin + slot * kItemStackSize));
    if (!name.empty()) diagLog(kTag, "slot %d = %s", slot, name.c_str());
  }
}

bool equippedTotemIsExpected(void *player) {
  return isTotem(const_cast<void *>(offhandItem(player)));
}

} // namespace

InventorySwap &InventorySwap::instance() {
  static InventorySwap instance;
  return instance;
}

void InventorySwap::reset() {
  g_action.store(static_cast<int>(Pending::None), std::memory_order_relaxed);
  g_lastResult.store(0, std::memory_order_relaxed);
  g_status.store(0, std::memory_order_relaxed);
  g_originalSlot.store(-1, std::memory_order_relaxed);
  g_armed.store(false, std::memory_order_relaxed);
}

void InventorySwap::request(bool enabled) {
  // C++ HTTP status устанавливается через таблицу compact-int и читается.
  g_status.store(enabled ? 1 : 0, std::memory_order_relaxed);
  g_action.store(static_cast<int>(enabled ? Pending::On : Pending::Off),
                 std::memory_order_release);
}

void InventorySwap::update() {
  Pending pending = static_cast<Pending>(g_action.exchange(
      static_cast<int>(Pending::None), std::memory_order_acq_rel));
  if (pending == Pending::None) return;
  void *player = ClientBridge::instance().getLocalPlayer();
  if (!player) {
    m_armed = false;
    g_armed.store(false, std::memory_order_relaxed);
    g_originalSlot.store(-1, std::memory_order_relaxed);
    g_lastResult.store(static_cast<int>(Outcome::NoPlayer),
                       std::memory_order_relaxed);
    g_status.store(static_cast<int>(Outcome::NoPlayer),
                   std::memory_order_relaxed);
    return;
  }
  if (pending == Pending::On) {
    if (equippedTotemIsExpected(player)) {
      if (g_originalSlot.load(std::memory_order_relaxed) >= 0) {
        g_armed.store(true, std::memory_order_relaxed);
      }
      m_armed = true;
      g_lastResult.store(static_cast<int>(Outcome::Enabled),
                         std::memory_order_relaxed);
      g_status.store(2, std::memory_order_relaxed);
      return;
    }
    void *inventory = playerInventory(player);
    int source = -1;
    if (inventory) {
      for (int slot = 0; slot < static_cast<int>(kMaxSlots); ++slot) {
        void *stack = itemAt(inventory, slot);
        if (isTotem(stack)) {
          source = slot;
          break;
        }
      }
    }
    if (source < 0) {
      dumpInventory(player, inventory);
      m_armed = false;
      g_armed.store(false, std::memory_order_relaxed);
      g_originalSlot.store(-1, std::memory_order_relaxed);
      g_lastResult.store(static_cast<int>(inventory ? Outcome::NoItem
                                                   : Outcome::NoContainer),
                         std::memory_order_relaxed);
      g_status.store(static_cast<int>(inventory ? Outcome::NoItem
                                                : Outcome::NoContainer),
                     std::memory_order_relaxed);
      return;
    }
    const void *previous = offhandItem(player);
    void *sourceItem = itemAt(inventory, source);
    if (!previous || !sourceItem) {
      g_lastResult.store(static_cast<int>(Outcome::NoContainer),
                         std::memory_order_relaxed);
      g_status.store(static_cast<int>(Outcome::NoContainer),
                     std::memory_order_relaxed);
      return;
    }
    // Порядок важен: игровой setter offhand вызывает отдельное событие
    // экипировки, затем контейнер получает старый offhand. Это настоящий swap,
    // а не копия предмета и не создание пустой ItemStack через FFI.
    if (!equipOffhand(player, sourceItem)) {
      g_lastResult.store(static_cast<int>(Outcome::NoContainer),
                         std::memory_order_relaxed);
      g_status.store(static_cast<int>(Outcome::NoContainer),
                     std::memory_order_relaxed);
      return;
    }
    if (!setInventorySlot(inventory, source, previous)) {
      g_lastResult.store(static_cast<int>(Outcome::NoContainer),
                         std::memory_order_relaxed);
      g_status.store(static_cast<int>(Outcome::NoContainer),
                     std::memory_order_relaxed);
      return;
    }
    g_originalSlot.store(source, std::memory_order_relaxed);
    m_armed = true;
    g_armed.store(true, std::memory_order_relaxed);
    g_lastResult.store(static_cast<int>(Outcome::Enabled),
                       std::memory_order_relaxed);
    g_status.store(2, std::memory_order_relaxed);
    diagLog(kTag, "swap: totem from slot %d -> offhand; previous -> slot %d",
            source, source);
    return;
  }

  // Off. Возвращаем старый offhand в исходный слот, а totem убираем из руки.
  int original = g_originalSlot.load(std::memory_order_relaxed);
  void *inventory = playerInventory(player);
  const void *current = offhandItem(player);
  if (original >= 0 && inventory && current) {
    void *returnedItem = itemAt(inventory, original);
    if (returnedItem) {
      // returnedItem — прежний offhand (или пустой stack), поэтому
      // возвращение идёт симметрично и без конструирования пустого стека.
      equipOffhand(player, returnedItem);
      setInventorySlot(inventory, original, current);
      diagLog(kTag, "restore: offhand -> slot %d; previous offhand restored",
              original);
    }
  }
  m_armed = false;
  g_armed.store(false, std::memory_order_relaxed);
  g_originalSlot.store(-1, std::memory_order_relaxed);
  g_lastResult.store(static_cast<int>(Outcome::Restored),
                     std::memory_order_relaxed);
  g_status.store(0, std::memory_order_relaxed);
}

int InventorySwap::status() const {
  return g_status.load(std::memory_order_acquire);
}

int InventorySwap::lastResult() const {
  return g_lastResult.load(std::memory_order_acquire);
}

} // namespace aimbot
