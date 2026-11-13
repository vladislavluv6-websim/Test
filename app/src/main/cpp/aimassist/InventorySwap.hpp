#pragma once

namespace aimbot {

class InventorySwap {
public:
  static InventorySwap &instance();

  void reset();
  void request(bool enabled);
  void update();

  int status() const;
  int lastResult() const;

private:
  InventorySwap() = default;
  enum class Outcome : int {
    Neutral = 0,
    Enabled = 1,
    Restored = 2,
    NoItem = -1,
    NoContainer = -2,
    NoPlayer = -3,
  };

  bool m_armed = false;
};

} // namespace aimbot
