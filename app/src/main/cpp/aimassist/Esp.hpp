#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include "ClientBridge.hpp"

namespace aimbot {

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

  // Called from the LevelRenderer::renderLevel detour.
  void render(void *levelRenderer, void *screenContext);

private:
  Esp() = default;

  ClientBridge &m_bridge = ClientBridge::instance();
  std::mutex m_initMutex;
  std::atomic<bool> m_initialized{false};
  std::atomic<bool> m_enabled{false};
  std::atomic<bool> m_showPlayers{true};
  std::atomic<bool> m_showMobs{true};
  std::atomic<float> m_range{32.0f};
  std::atomic<uint32_t> m_playerColor{0xFFFF5555u};
  std::atomic<uint32_t> m_mobColor{0xFFFFAA00u};
  std::vector<void *> m_actors;
  std::chrono::steady_clock::time_point m_lastLog{};
};

} // namespace aimbot
