#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ClientBridge.hpp"

namespace aimbot {

struct NameTagLabel {
  float x = 0.0f;
  float y = 0.0f;
  std::string name;
};

class Esp {
public:
  static Esp &instance();

  bool initialize();
  void shutdown();

  void setEnabled(bool enabled);
  bool isEnabled() const;

  void setPlayers(bool enabled);
  void setMobs(bool enabled);
  void setRange(float range);
  void setPlayerColor(uint32_t argb);
  void setMobColor(uint32_t argb);

  // Name Tags: экранные координаты считает native (game thread), Java
  // overlay только рисует готовые подписи.
  void setNameTags(bool enabled);
  bool isNameTagsEnabled() const;
  void setNameTagSurface(float width, float height);
  std::vector<NameTagLabel> copyNameTags() const;

  // Called from the LevelRenderer::renderLevel detour.
  void render(void *levelRenderer, void *screenContext);

private:
  Esp() = default;

  void updateNameTags(void *player, uintptr_t playerRenderer,
                      const float camera[3]);
  std::string actorName(void *actor, std::chrono::steady_clock::time_point now);

  ClientBridge &m_bridge = ClientBridge::instance();
  std::mutex m_initMutex;
  std::atomic<bool> m_initialized{false};
  std::atomic<bool> m_enabled{false};
  std::atomic<bool> m_showPlayers{true};
  std::atomic<bool> m_showMobs{true};
  std::atomic<float> m_range{32.0f};
  std::atomic<uint32_t> m_playerColor{0xFFFF5555u};
  std::atomic<uint32_t> m_mobColor{0xFFFFAA00u};
  std::atomic<bool> m_nameTags{true};
  std::atomic<float> m_surfaceWidth{0.0f};
  std::atomic<float> m_surfaceHeight{0.0f};
  std::vector<void *> m_actors;
  std::chrono::steady_clock::time_point m_lastLog{};
  mutable std::mutex m_nameMutex;
  std::vector<NameTagLabel> m_nameSnapshot;
  std::unordered_map<void *, std::pair<std::string,
                                       std::chrono::steady_clock::time_point>>
      m_nameCache;
};

} // namespace aimbot
