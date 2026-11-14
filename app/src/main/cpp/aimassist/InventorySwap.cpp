#include "InventorySwap.hpp"

#include <algorithm>
#include <atomic>
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
constexpr int kOffhandSlot = 1;
constexpr size_t kSetOffhandSlotVtableIndex = 79;
constexpr size_t kSetItemVtableIndex = 13;
constexpr size_t kItemGetDescriptionIdVtableIndex = 5;

const char *const kTag = "AutoSwap";

using ActorOffhandFn = void (*)(void *, const void *);
using ContainerSetItemFn = void (*)(void *, int, const void *);

enum class Pending : int { None = 0, On = 1, Off = 2 };
std::atomic<int> g_action{static_cast<int>(Pending::None)};
std::atomic<int> g_lastResult{0};
std::atomic<int> g_status{0};
std::atomic<int> g_originalSlot{-1};
std::atomic<bool> g_armed{false};

// Пустой ItemStack в памяти: поля нулевые, shared-указатели null. Нужен
// только как аргумент Container::setItem для очистки служебного слота.
alignas(16) unsigned char g_emptyItem[kItemStackSize]{};
const void *emptyItem() { return g_emptyItem; }

bool readPointer(uintptr_t address, uintptr_t &value) {
  if (!aimbot::mem::read(address, value)) return false;
  return aimbot::mem::isPlausiblePointer(value);
}

uintptr_t virtualFunction(void *object, size_t index) {
  uintptr_t vtable = 0;
  if (!object ||
      !aimbot::mem::read(reinterpret_cast<uintptr_t>(object), vtable) ||
      !aimbot::mem::isGameAddress(reinterpret_cast<void *>(vtable))) {
    return 0;
  }
  uintptr_t fn = 0;
  if (!aimbot::mem::read(vtable + index * sizeof(uintptr_t), fn)) return 0;
  return aimbot::mem::isGameAddress(reinterpret_cast<void *>(fn)) ? fn : 0;
}

bool inventoryItems(void *inventory, uintptr_t &begin, uintptr_t &end);

bool isEmptyStack(const void *stack) {
  if (!stack) return true;
  uint8_t count = 0;
  uint8_t valid = 0;
  if (!aimbot::mem::read(reinterpret_cast<uintptr_t>(stack) + 0x22, count) ||
      !aimbot::mem::read(reinterpret_cast<uintptr_t>(stack) + 0x23, valid)) {
    return true;
  }
  return count == 0 || valid == 0;
}

void *validInventory(void *candidate) {
  if (!candidate) return nullptr;
  if (!virtualFunction(candidate, 8)) return nullptr;
  uintptr_t begin = 0;
  uintptr_t end = 0;
  if (!inventoryItems(candidate, begin, end)) return nullptr;
  return candidate;
}

void *scanForInventory(void *object, uintptr_t first, uintptr_t last) {
  if (!object) return nullptr;
  const uintptr_t base = reinterpret_cast<uintptr_t>(object);
  for (uintptr_t offset = first; offset <= last; offset += sizeof(uintptr_t)) {
    uintptr_t candidate = 0;
    if (!readPointer(base + offset, candidate)) continue;
    if (void *valid = validInventory(reinterpret_cast<void *>(candidate))) {
      return valid;
    }
  }
  return nullptr;
}

void *playerInventory(void *player) {
  uintptr_t proxy = 0;
  uintptr_t container = 0;
  if (readPointer(reinterpret_cast<uintptr_t>(player) + kPlayerInventory,
                  proxy) &&
      readPointer(proxy + kPlayerInventoryContainer, container)) {
    if (void *valid = validInventory(reinterpret_cast<void *>(container))) {
      return valid;
    }
    if (void *valid = scanForInventory(reinterpret_cast<void *>(proxy), 0x20,
                                       0x300)) {
      return valid;
    }
  }
  if (void *valid = scanForInventory(player, 0x400, 0x900)) return valid;
  return validInventory(gameecs::handItemContainer(player));
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
  return count >= kMaxSlots && count <= 64;
}

void *itemAt(void *inventory, int slot) {
  uintptr_t begin = 0;
  uintptr_t end = 0;
  if (slot < 0 || slot >= static_cast<int>(kMaxSlots) ||
      !inventoryItems(inventory, begin, end)) {
    return nullptr;
  }
  return reinterpret_cast<void *>(
      begin + static_cast<uintptr_t>(slot) * kItemStackSize);
}

int findEmptySlot(void *inventory, int excluded) {
  for (int slot = 0; slot < static_cast<int>(kMaxSlots); ++slot) {
    if (slot == excluded) continue;
    void *stack = itemAt(inventory, slot);
    if (stack && isEmptyStack(stack)) return slot;
  }
  return -1;
}

const void *offhandItem(void *player) {
  return gameecs::handItemStack(player, kOffhandSlot);
}

bool equipOffhand(void *player, const void *item) {
  const uintptr_t fn = virtualFunction(player, kSetOffhandSlotVtableIndex);
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
  if (identifier.empty()) {
    identifier = gameecs::stackItemName(reinterpret_cast<uintptr_t>(stack));
  }
  return identifier;
}

bool isTotemName(std::string identifier) {
  std::replace(identifier.begin(), identifier.end(), ' ', '_');
  for (char &ch : identifier) {
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
  }
  return identifier.find("totem_of_undying") != std::string::npos;
}

bool isTotem(void *stack) {
  // Оба способа проверяются всегда: getDescriptionId может вернуть непустой,
  // но не тот identifier (descriptor/локализованное имя).
  if (isTotemName(itemIdentifier(stack))) return true;
  return isTotemName(
      gameecs::stackItemName(reinterpret_cast<uintptr_t>(stack)));
}

bool equippedTotemIsExpected(void *player) {
  return isTotem(const_cast<void *>(offhandItem(player)));
}

void setError(int outcome) {
  g_lastResult.store(outcome, std::memory_order_relaxed);
  g_status.store(outcome, std::memory_order_relaxed);
  g_armed.store(false, std::memory_order_relaxed);
}

void setEquipped(int slot) {
  g_originalSlot.store(slot, std::memory_order_relaxed);
  g_lastResult.store(1, std::memory_order_relaxed);
  g_status.store(2, std::memory_order_relaxed);
  g_armed.store(true, std::memory_order_relaxed);
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
  diagLog(kTag, "request: enabled=%d previous_status=%d original_slot=%d",
          enabled ? 1 : 0, g_status.load(std::memory_order_relaxed),
          g_originalSlot.load(std::memory_order_relaxed));
  g_status.store(enabled ? 1 : 0, std::memory_order_relaxed);
  g_action.store(static_cast<int>(enabled ? Pending::On : Pending::Off),
                 std::memory_order_release);
}

void InventorySwap::update() {
  const Pending pending = static_cast<Pending>(g_action.exchange(
      static_cast<int>(Pending::None), std::memory_order_acq_rel));
  if (pending == Pending::None) return;

  void *player = ClientBridge::instance().getLocalPlayer();
  if (!player) {
    setError(-3);
    g_originalSlot.store(-1, std::memory_order_relaxed);
    return;
  }
  void *inventory = playerInventory(player);
  if (!inventory) {
    diagLog(kTag, "container unavailable player=%p", player);
    setError(-2);
    return;
  }

  if (pending == Pending::On) {
    if (equippedTotemIsExpected(player)) {
      // Уже надет вручную/после предыдущего включения. Для выключения
      // запоминаем свободный слот, чтобы было куда вернуть тотем.
      if (g_originalSlot.load(std::memory_order_relaxed) < 0) {
        const int empty = findEmptySlot(inventory, -1);
        if (empty >= 0) g_originalSlot.store(empty, std::memory_order_relaxed);
      }
      g_lastResult.store(1, std::memory_order_relaxed);
      g_status.store(2, std::memory_order_relaxed);
      g_armed.store(true, std::memory_order_relaxed);
      return;
    }

    int source = -1;
    for (int slot = 0; slot < static_cast<int>(kMaxSlots); ++slot) {
      if (isTotem(itemAt(inventory, slot))) {
        source = slot;
        break;
      }
    }
    if (source < 0) {
      dumpInventory(player, inventory);
      setError(-1);
      return;
    }

    void *sourceItem = itemAt(inventory, source);
    const void *oldOffhand = offhandItem(player);
    if (!sourceItem || !oldOffhand) {
      diagLog(kTag, "offhand/source stack unavailable source=%p old=%p", source,
              sourceItem, oldOffhand);
      setError(-2);
      return;
    }

    // oldOffhand нельзя передавать в setItem после setOffhandSlot: setter
    // пишет в тот же ItemStack и затирает предыдущий предмет. Сохраняем его
    // в свободный слот до подмены. Пустой offhand не требует temp.
    int temp = -1;
    if (!isEmptyStack(oldOffhand)) {
      temp = findEmptySlot(inventory, source);
      if (temp < 0 || !setInventorySlot(inventory, temp, oldOffhand)) {
        diagLog(kTag, "no space to preserve previous offhand");
        setError(-2);
        return;
      }
    }

    if (!equipOffhand(player, sourceItem)) {
      diagLog(kTag, "Actor::setOffhandSlot failed");
      setError(-2);
      return;
    }

    bool ok = true;
    if (temp >= 0) {
      ok = setInventorySlot(inventory, source, itemAt(inventory, temp)) &&
           setInventorySlot(inventory, temp, emptyItem());
    } else {
      ok = setInventorySlot(inventory, source, emptyItem());
    }
    if (!ok) {
      diagLog(kTag, "inventory restore after equip failed");
      setError(-2);
      return;
    }

    setEquipped(source);
    diagLog(kTag, "equipped totem from slot %d; old offhand saved in %d",
            source, temp);
    return;
  }

  // Off. Если тотем уже был в руке до включения (originalSlot < 0), просто
  // перекладываем его в первый свободный слот и очищаем offhand.
  int original = g_originalSlot.load(std::memory_order_relaxed);
  const void *current = offhandItem(player);
  if (!isTotem(const_cast<void *>(current))) {
    // Пользователь вручную убрал/сменил тотем — не трогаем его предмет.
    g_originalSlot.store(-1, std::memory_order_relaxed);
    g_lastResult.store(2, std::memory_order_relaxed);
    g_status.store(0, std::memory_order_relaxed);
    g_armed.store(false, std::memory_order_relaxed);
    return;
  }

  if (original < 0) {
    const int empty = findEmptySlot(inventory, -1);
    if (empty < 0 || !setInventorySlot(inventory, empty, current) ||
        !equipOffhand(player, emptyItem())) {
      diagLog(kTag, "no space to return manually equipped totem");
      setError(-2);
      return;
    }
    g_originalSlot.store(-1, std::memory_order_relaxed);
    g_lastResult.store(2, std::memory_order_relaxed);
    g_status.store(0, std::memory_order_relaxed);
    g_armed.store(false, std::memory_order_relaxed);
    return;
  }

  void *returnedItem = itemAt(inventory, original);
  if (!returnedItem) {
    setError(-2);
    return;
  }

  if (isEmptyStack(returnedItem)) {
    // Прежний offhand был пуст: сначала копируем тотем в исходный слот,
    // затем очищаем руку.
    if (!setInventorySlot(inventory, original, current) ||
        !equipOffhand(player, emptyItem())) {
      diagLog(kTag, "restore empty offhand failed");
      setError(-2);
      return;
    }
  } else {
    // Прежний offhand не пуст. Сначала сохраняем его во временный слот,
    // затем копируем тотем в исходный слот и возвращаем старый offhand.
    const int temp = findEmptySlot(inventory, original);
    if (temp < 0 || !setInventorySlot(inventory, temp, returnedItem) ||
        !setInventorySlot(inventory, original, current)) {
      diagLog(kTag, "restore non-empty offhand failed temp=%d", temp);
      setError(-2);
      return;
    }
    if (!equipOffhand(player, itemAt(inventory, temp)) ||
        !setInventorySlot(inventory, temp, emptyItem())) {
      diagLog(kTag, "restore old offhand from temp failed");
      setError(-2);
      return;
    }
  }

  g_originalSlot.store(-1, std::memory_order_relaxed);
  g_lastResult.store(2, std::memory_order_relaxed);
  g_status.store(0, std::memory_order_relaxed);
  g_armed.store(false, std::memory_order_relaxed);
  diagLog(kTag, "restored offhand; totem returned to slot %d", original);
}

int InventorySwap::status() const {
  return g_status.load(std::memory_order_acquire);
}

int InventorySwap::lastResult() const {
  return g_lastResult.load(std::memory_order_acquire);
}

} // namespace aimbot