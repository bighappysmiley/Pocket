#pragma once
#include "pocket/canvas.hpp"
#include "pocket/refresh.hpp"
#include <cstdint>

namespace pocket::board {

class EpdDisplay {
 public:
  bool init();
  void present(const pocket::Canvas& canvas, pocket::RefreshMode mode);
  /** Partial update for a logical (portrait) rectangle — used for Home clock ticks. */
  void present_region(const pocket::Canvas& canvas, int lx, int ly, int lw, int lh);
  void sleep();

 private:
  void rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst);

  bool ready_ = false;
  uint8_t* panel_1bpp_ = nullptr;
};

}  // namespace pocket::board
