#pragma once
#include "pocket/input.hpp"
#include <cstdint>

namespace pocket::board {

/** Poll Part C buttons (active-low) into an InputMapper. */
class ButtonPoller {
 public:
  void init();
  /** Sample GPIOs and push edges + long-press ticks into mapper. */
  void poll(pocket::InputMapper& mapper, uint32_t now_ms);

 private:
  bool last_up_ = false;
  bool last_down_ = false;
  bool last_fn_ = false;
  bool last_boot_ = false;
  bool last_pwr_ = false;
  bool inited_ = false;
};

}  // namespace pocket::board
