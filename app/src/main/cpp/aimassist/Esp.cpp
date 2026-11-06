#include "Esp.hpp"

#include <android/log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

#include "Memory.hpp"
#include "Offsets.hpp"
#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

namespace aimbot {
namespace {

constexpr const char *kTag = "AimAssistEsp";

// mce::PrimitiveMode
constexpr int kPrimitiveQuadList = 1;
constexpr int kPrimitiveLineList = 4;

// Прозрачность заливки относительно альфы выбранного цвета.
constexpr float kFillAlpha = 0.38f;
// Чуть раздуваем бокс, чтобы заливка не мерцала на модели.
constexpr float kInflate = 0.02f;

using TessellatorBeginFn = void (*)(void *, void *, int, int, int);
using TessellatorColorFn = void (*)(void *, float, float, float, float);
using TessellatorVertexFn = void (*)(void *, float, float, float);
using RenderMeshFn = void (*)(void *, void *, void *, char *);
using RenderLevelFn = void (*)(void *, void *, void *);

TessellatorBeginFn g_tessBegin = nullptr;
TessellatorColorFn g_tessColor = nullptr;
TessellatorVertexFn g_tessVertex = nullptr;
RenderMeshFn g_renderMesh = nullptr;
RenderLevelFn g_renderLevelOriginal = nullptr;

// ------------------------------------------------------------
// Материалы
// ------------------------------------------------------------
// Раскладка как в игре (libc++): hash, std::string, lastMatch.
struct HashedString {
  uint64_t hash = 0;
  std::string str;
  const HashedString *lastMatch = nullptr;

  explicit HashedString(const char *value) : str(value ? value : "") {
    if (str.empty()) return;
    constexpr uint64_t kOffset = 0xCBF29CE484222325ULL;
    constexpr uint64_t kPrime = 0x100000001B3ULL;
    uint64_t h = kOffset;
    for (char ch : str) {
      h = static_cast<uint64_t>(static_cast<unsigned char>(ch)) ^ (kPrime * h);
    }
    hash = h;
  }
};

// mce::MaterialPtr = shared_ptr. Нетривиальный деструктор нужен, чтобы
// ABI возвращал его через x8, как в игре. Ссылку намеренно не отпускаем:
// материал живёт всё время работы игры.
struct MaterialPtr {
  void *data[2]{nullptr, nullptr};

  MaterialPtr() = default;
  MaterialPtr(const MaterialPtr &) = delete;
  MaterialPtr &operator=(const MaterialPtr &) = delete;
  MaterialPtr(MaterialPtr &&other) noexcept {
    data[0] = other.data[0];
    data[1] = other.data[1];
    other.data[0] = nullptr;
    other.data[1] = nullptr;
  }
  MaterialPtr &operator=(MaterialPtr &&other) noexcept {
    if (this != &other) {
      data[0] = other.data[0];
      data[1] = other.data[1];
      other.data[0] = nullptr;
      other.data[1] = nullptr;
    }
    return *this;
  }
  ~MaterialPtr() {}

  explicit operator bool() const { return data[0] != nullptr; }
};

using GetMaterialFn = MaterialPtr (*)(void *, const HashedString *);

uintptr_t g_materialGroup = 0;
MaterialPtr g_xrayMaterial;
const char *g_xrayMaterialName = nullptr;
int g_materialAttempts = 0;
int g_materialRetryFrames = 0;

// Материалы без depth-test идут первыми: так бокс виден сквозь стены.
constexpr const char *kXrayMaterials[] = {
    "name_tag",
    "ui_fill_color",
    "selection_overlay",
    "selection_box",
};

// insns — локальная копия инструкций, pc — их реальный адрес в .text.
uintptr_t resolveAdrp(const uint32_t *insns, uintptr_t pc, size_t count,
                      uint32_t targetReg) {
  for (size_t i = 0; i < count; i++) {
    const uint32_t insn = insns[i];
    if ((insn & 0x1F) != targetReg) continue;

    if ((insn & 0x9F000000) == 0x90000000) {
      const int64_t immhi = (insn >> 5) & 0x7FFFF;
      const int64_t immlo = (insn >> 29) & 0x3;
      int64_t imm = (immhi << 2) | immlo;
      if (imm & (1LL << 20)) imm -= (1LL << 21);
      const uintptr_t page = ((pc + i * 4) & ~0xFFFULL) +
                             static_cast<uintptr_t>(imm * 4096);

      for (size_t j = i + 1; j < count; j++) {
        const uint32_t add = insns[j];
        if ((add & 0xFF000000) == 0x91000000 &&
            ((add >> 5) & 0x1F) == targetReg && (add & 0x1F) == targetReg) {
          uint32_t imm12 = (add >> 10) & 0xFFF;
          if (add & 0x400000) imm12 <<= 12;
          return page + imm12;
        }
        if ((add & 0x1F) == targetReg) break;
      }
    }
  }
  return 0;
}

void resolveMaterialGroup() {
  const uintptr_t fn = pl::memory::resolveSignature(
      offsets::kRenderMaterialGroupCommonSignature, offsets::kMinecraftLibrary);
  if (!fn || !mem::isGameAddress(reinterpret_cast<const void *>(fn))) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "RenderMaterialGroup signature not found");
    return;
  }
  uint32_t insns[8]{};
  if (!mem::read(fn, insns, sizeof(insns))) return;
  const uintptr_t group = resolveAdrp(insns, fn, 8, 0);
  if (!group) {
    __android_log_print(ANDROID_LOG_WARN, kTag, "RenderMaterialGroup ADRP failed");
    return;
  }
  g_materialGroup = group + offsets::kRenderMaterialGroupOffset;
  __android_log_print(ANDROID_LOG_INFO, kTag, "RenderMaterialGroup=%p",
                      reinterpret_cast<void *>(g_materialGroup));
}

bool lookupMaterial(const char *name, MaterialPtr &out) {
  if (!g_materialGroup) return false;
  uintptr_t vtable = 0;
  if (!mem::read(g_materialGroup, vtable) || !mem::isPlausiblePointer(vtable)) {
    return false;
  }
  uintptr_t getMaterial = 0;
  if (!mem::read(vtable + 2 * sizeof(void *), getMaterial) ||
      !mem::isGameAddress(reinterpret_cast<const void *>(getMaterial))) {
    return false;
  }
  HashedString key(name);
  out = reinterpret_cast<GetMaterialFn>(getMaterial)(
      reinterpret_cast<void *>(g_materialGroup), &key);
  return static_cast<bool>(out);
}

void ensureXrayMaterial() {
  if (g_xrayMaterial || !g_materialGroup) return;
  if (g_materialRetryFrames > 0) {
    --g_materialRetryFrames;
    return;
  }
  if (g_materialAttempts >= 30) return;
  ++g_materialAttempts;
  g_materialRetryFrames = 120;

  for (const char *name : kXrayMaterials) {
    MaterialPtr material;
    if (lookupMaterial(name, material)) {
      g_xrayMaterial = std::move(material);
      g_xrayMaterialName = name;
      __android_log_print(ANDROID_LOG_INFO, kTag, "ESP material: %s", name);
      return;
    }
  }
}

// ------------------------------------------------------------
// Цвет шейдера
// ------------------------------------------------------------
struct ColorState {
  uintptr_t holder = 0;
  std::byte saved[16]{};

  explicit ColorState(void *screenContext) {
    uintptr_t context = reinterpret_cast<uintptr_t>(screenContext);
    if (!mem::isPlausiblePointer(context)) return;
    if (!mem::read(context + offsets::kScreenContextColorHolder, holder) ||
        !mem::isPlausiblePointer(holder)) {
      holder = 0;
      return;
    }
    if (!mem::read(holder, saved, sizeof(saved))) {
      holder = 0;
      return;
    }
    const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (!mem::write(holder, white, sizeof(white))) {
      holder = 0;
      return;
    }
    markDirty();
  }

  ~ColorState() {
    if (!holder) return;
    if (mem::write(holder, saved, sizeof(saved))) markDirty();
  }

  explicit operator bool() const { return holder != 0; }

  void markDirty() const {
    const uint8_t dirty = 1;
    mem::write(holder + offsets::kShaderColorDirty, dirty);
  }
};

void renderLevelDetour(void *levelRenderer, void *screenContext, void *a3) {
  if (g_renderLevelOriginal) {
    g_renderLevelOriginal(levelRenderer, screenContext, a3);
  }
  Esp::instance().render(levelRenderer, screenContext);
}

bool validGameFn(uintptr_t address) {
  return address != 0 &&
         mem::isGameAddress(reinterpret_cast<const void *>(address));
}

struct Rgba {
  float r, g, b, a;
};

Rgba toRgba(uint32_t argb) {
  return {static_cast<float>((argb >> 16) & 0xFF) / 255.0f,
          static_cast<float>((argb >> 8) & 0xFF) / 255.0f,
          static_cast<float>(argb & 0xFF) / 255.0f,
          static_cast<float>((argb >> 24) & 0xFF) / 255.0f};
}

void submit(void *tessellator, void *screenContext, void *material) {
  char pad[0x58];
  std::memset(pad, 0, sizeof(pad));
  g_renderMesh(screenContext, tessellator, material, pad);
}

// Заливка: 6 граней, каждая в обе стороны (без зависимости от culling).
void drawFilledBox(void *tessellator, void *screenContext, void *material,
                   const Vec3 c[8], const Rgba &color) {
  static constexpr int kFaces[6][4] = {
      {0, 1, 2, 3}, // низ
      {4, 7, 6, 5}, // верх
      {0, 4, 5, 1}, // север
      {2, 6, 7, 3}, // юг
      {3, 7, 4, 0}, // запад
      {1, 5, 6, 2}, // восток
  };

  g_tessBegin(tessellator, nullptr, kPrimitiveQuadList, 48, 0);
  g_tessColor(tessellator, color.r, color.g, color.b, color.a * kFillAlpha);
  for (const auto &face : kFaces) {
    for (int i = 0; i < 4; ++i) {
      const Vec3 &p = c[face[i]];
      g_tessVertex(tessellator, p.x, p.y, p.z);
    }
    for (int i = 3; i >= 0; --i) {
      const Vec3 &p = c[face[i]];
      g_tessVertex(tessellator, p.x, p.y, p.z);
    }
  }
  submit(tessellator, screenContext, material);
}

void drawOutline(void *tessellator, void *screenContext, void *material,
                 const Vec3 c[8], const Rgba &color) {
  static constexpr int kEdges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
      {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };
  g_tessBegin(tessellator, nullptr, kPrimitiveLineList, 24, 0);
  g_tessColor(tessellator, color.r, color.g, color.b, color.a);
  for (const auto &edge : kEdges) {
    const Vec3 &p1 = c[edge[0]];
    const Vec3 &p2 = c[edge[1]];
    g_tessVertex(tessellator, p1.x, p1.y, p1.z);
    g_tessVertex(tessellator, p2.x, p2.y, p2.z);
  }
  submit(tessellator, screenContext, material);
}

void drawBox(void *tessellator, void *screenContext, void *material,
             const Aabb &aabb, const float camera[3], uint32_t argb) {
  if (!tessellator || !screenContext || !material) return;

  const float x0 = aabb.min.x - kInflate - camera[0];
  const float y0 = aabb.min.y - kInflate - camera[1];
  const float z0 = aabb.min.z - kInflate - camera[2];
  const float x1 = aabb.max.x + kInflate - camera[0];
  const float y1 = aabb.max.y + kInflate - camera[1];
  const float z1 = aabb.max.z + kInflate - camera[2];
  const Vec3 corners[8] = {
      {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1},
      {x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1},
  };

  const Rgba color = toRgba(argb);
  drawFilledBox(tessellator, screenContext, material, corners, color);
  drawOutline(tessellator, screenContext, material, corners, color);
}

} // namespace

Esp &Esp::instance() {
  static Esp esp;
  return esp;
}

bool Esp::initialize() {
  std::lock_guard<std::mutex> lock(m_initMutex);
  if (m_initialized.load(std::memory_order_acquire)) return true;

  if (!m_bridge.initialize()) {
    __android_log_print(ANDROID_LOG_WARN, kTag,
                        "ClientBridge init failed");
    return false;
  }

  const uintptr_t renderLevel =
      pl::memory::resolveSignature(offsets::kRenderLevelSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t tessBegin =
      pl::memory::resolveSignature(offsets::kTessellatorBeginSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t tessColor =
      pl::memory::resolveSignature(offsets::kTessellatorColorSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t tessVertex =
      pl::memory::resolveSignature(offsets::kTessellatorVertexSignature,
                                   offsets::kMinecraftLibrary);
  const uintptr_t renderMesh =
      pl::memory::resolveSignature(offsets::kRenderMeshImmediatelySignature,
                                   offsets::kMinecraftLibrary);

  if (!validGameFn(renderLevel) || !validGameFn(tessBegin) ||
      !validGameFn(tessColor) || !validGameFn(tessVertex) ||
      !validGameFn(renderMesh)) {
    __android_log_print(
        ANDROID_LOG_WARN, kTag,
        "ESP signatures failed: render=%p begin=%p color=%p vertex=%p mesh=%p",
        reinterpret_cast<void *>(renderLevel),
        reinterpret_cast<void *>(tessBegin),
        reinterpret_cast<void *>(tessColor),
        reinterpret_cast<void *>(tessVertex),
        reinterpret_cast<void *>(renderMesh));
    return false;
  }

  g_tessBegin = reinterpret_cast<TessellatorBeginFn>(tessBegin);
  g_tessColor = reinterpret_cast<TessellatorColorFn>(tessColor);
  g_tessVertex = reinterpret_cast<TessellatorVertexFn>(tessVertex);
  g_renderMesh = reinterpret_cast<RenderMeshFn>(renderMesh);

  resolveMaterialGroup();

  if (pl::memory::hook(
          reinterpret_cast<pl::memory::FuncPtr>(renderLevel),
          reinterpret_cast<pl::memory::FuncPtr>(&renderLevelDetour),
          reinterpret_cast<pl::memory::FuncPtr *>(&g_renderLevelOriginal)) !=
      0) {
    __android_log_print(ANDROID_LOG_WARN, kTag, "renderLevel hook failed");
    return false;
  }

  m_initialized.store(true, std::memory_order_release);
  __android_log_print(ANDROID_LOG_INFO, kTag,
                      "ESP initialized: render=%p", (void *)renderLevel);
  return true;
}

void Esp::shutdown() {
  m_enabled.store(false, std::memory_order_release);
}

void Esp::setEnabled(bool enabled) {
  m_enabled.store(enabled, std::memory_order_release);
}

bool Esp::isEnabled() const {
  return m_enabled.load(std::memory_order_acquire);
}

void Esp::setPlayers(bool enabled) {
  m_showPlayers.store(enabled, std::memory_order_release);
}

void Esp::setMobs(bool enabled) {
  m_showMobs.store(enabled, std::memory_order_release);
}

void Esp::setRange(float range) {
  if (!std::isfinite(range)) return;
  m_range.store(std::clamp(range, 4.0f, 96.0f), std::memory_order_release);
}

void Esp::setPlayerColor(uint32_t argb) {
  m_playerColor.store(argb, std::memory_order_release);
}

void Esp::setMobColor(uint32_t argb) {
  m_mobColor.store(argb, std::memory_order_release);
}

void Esp::render(void *levelRenderer, void *screenContext) {
  if (!m_initialized.load(std::memory_order_acquire) ||
      !m_enabled.load(std::memory_order_acquire)) {
    return;
  }
  if (!mem::isPlausiblePointer(reinterpret_cast<uintptr_t>(levelRenderer)) ||
      !mem::isPlausiblePointer(reinterpret_cast<uintptr_t>(screenContext))) {
    return;
  }

  const bool showPlayers = m_showPlayers.load(std::memory_order_relaxed);
  const bool showMobs = m_showMobs.load(std::memory_order_relaxed);
  if (!showPlayers && !showMobs) return;

  void *player = m_bridge.getLocalPlayer();
  if (!player) return;

  uintptr_t screen = reinterpret_cast<uintptr_t>(screenContext);
  uintptr_t tessellator = 0;
  if (!mem::read(screen + offsets::kScreenContextTessellator, tessellator) ||
      !mem::isPlausiblePointer(tessellator)) {
    return;
  }
  uintptr_t probe = 0;
  if (!mem::read(tessellator, probe)) return;

  uintptr_t level = reinterpret_cast<uintptr_t>(levelRenderer);
  uintptr_t playerRenderer = 0;
  if (!mem::read(level + offsets::kLevelRendererPlayer, playerRenderer) ||
      !mem::isPlausiblePointer(playerRenderer)) {
    return;
  }

  float camera[3]{};
  if (!mem::read(playerRenderer + offsets::kLevelRendererPlayerCamPos, camera)) {
    return;
  }
  if (!std::isfinite(camera[0]) || !std::isfinite(camera[1]) ||
      !std::isfinite(camera[2])) {
    return;
  }

  ensureXrayMaterial();
  void *material =
      g_xrayMaterial
          ? static_cast<void *>(&g_xrayMaterial)
          : reinterpret_cast<void *>(playerRenderer +
                                     offsets::kLevelRendererSelectionMaterial);

  ColorState colorState(screenContext);
  if (!colorState) return;

  const uint32_t playerColor = m_playerColor.load(std::memory_order_relaxed);
  const uint32_t mobColor = m_mobColor.load(std::memory_order_relaxed);

  m_bridge.fillNearbyActors(player, m_range.load(std::memory_order_relaxed),
                            m_actors);
  int drawn = 0;
  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;

    const bool isPlayer = m_bridge.isPlayer(actor);
    if (isPlayer && !showPlayers) continue;
    if (!isPlayer) {
      if (!showMobs) continue;
      const uint32_t categories = m_bridge.getActorCategories(actor);
      if ((categories & offsets::kMobCategory) == 0) continue;
    }

    Aabb bounds{};
    if (!m_bridge.getActorBounds(actor, bounds)) continue;
    drawBox(reinterpret_cast<void *>(tessellator), screenContext, material,
            bounds, camera, isPlayer ? playerColor : mobColor);
    ++drawn;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - m_lastLog >= std::chrono::seconds(5)) {
    m_lastLog = now;
    __android_log_print(ANDROID_LOG_INFO, kTag,
                        "ESP frame: actors=%zu drawn=%d material=%s",
                        m_actors.size(), drawn,
                        g_xrayMaterialName ? g_xrayMaterialName
                                           : "selection_overlay(lrp)");
  }
}

} // namespace aimbot
