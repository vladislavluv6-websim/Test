#include "Esp.hpp"

#include <android/log.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Memory.hpp"
#include "Offsets.hpp"
#include "pl/memory/Hook.hpp"
#include "pl/memory/Signature.hpp"

namespace aimbot {
namespace {

constexpr const char *kTag = "AimAssistEsp";

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

void drawBox(void *tessellator, void *screenContext, void *material,
             const Aabb &aabb, const float camera[3], uint32_t argb) {
  if (!tessellator || !screenContext || !material) return;

  const Vec3 &mn = aabb.min;
  const Vec3 &mx = aabb.max;
  const Vec3 corners[8] = {
      {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z},
      {mn.x, mn.y, mx.z}, {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
      {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z},
  };
  static constexpr int kEdges[12][2] = {
      {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
      {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7},
  };

  const float r = static_cast<float>((argb >> 16) & 0xFF) / 255.0f;
  const float g = static_cast<float>((argb >> 8) & 0xFF) / 255.0f;
  const float b = static_cast<float>(argb & 0xFF) / 255.0f;
  const float a = static_cast<float>((argb >> 24) & 0xFF) / 255.0f;

  g_tessBegin(tessellator, nullptr, 4, 24, 0);
  g_tessColor(tessellator, r, g, b, a);
  for (const auto &edge : kEdges) {
    const Vec3 &p1 = corners[edge[0]];
    const Vec3 &p2 = corners[edge[1]];
    g_tessVertex(tessellator, p1.x - camera[0], p1.y - camera[1],
                 p1.z - camera[2]);
    g_tessVertex(tessellator, p2.x - camera[0], p2.y - camera[1],
                 p2.z - camera[2]);
  }

  char pad[0x58];
  std::memset(pad, 0, sizeof(pad));
  g_renderMesh(screenContext, tessellator, material, pad);
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

  void *player = m_bridge.getLocalPlayer();
  if (!player) return;
  Vec3 playerPosition{};
  if (!m_bridge.getPosition(player, playerPosition)) return;

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

  ColorState colorState(screenContext);
  if (!colorState) return;

  void *material = reinterpret_cast<void *>(
      playerRenderer + offsets::kLevelRendererSelectionMaterial);

  const bool showPlayers = m_showPlayers.load(std::memory_order_relaxed);
  const bool showMobs = m_showMobs.load(std::memory_order_relaxed);
  const uint32_t playerColor = m_playerColor.load(std::memory_order_relaxed);
  const uint32_t mobColor = m_mobColor.load(std::memory_order_relaxed);
  if (!showPlayers && !showMobs) return;

  m_bridge.fillNearbyActors(player, m_range.load(std::memory_order_relaxed),
                            m_actors);
  for (void *actor : m_actors) {
    if (!actor || actor == player) continue;

    const bool isPlayer = m_bridge.isPlayer(actor);
    if (isPlayer && !showPlayers) continue;
    if (!isPlayer) {
      if (!showMobs) continue;
      const uint32_t categories = m_bridge.getActorCategories(actor);
      if ((categories & offsets::kMobCategory) == 0) continue;
    }

    Aabb aabb{};
    if (!m_bridge.getActorAabb(actor, aabb)) continue;
    drawBox(reinterpret_cast<void *>(tessellator), screenContext, material,
            aabb, camera, isPlayer ? playerColor : mobColor);
  }
}

} // namespace aimbot
