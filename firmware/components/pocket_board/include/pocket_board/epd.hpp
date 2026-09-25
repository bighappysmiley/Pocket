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

  /** 0–100 → gray threshold 1..3 (of the canvas's 4-level Gray). Higher percent = more midtone
   * pixels round to white, so the page reads lighter. 50% keeps the historical default (2). */
  void set_brightness(int percent);

 private:
  void rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst);

  bool ready_ = false;
  uint8_t* panel_1bpp_ = nullptr;
  uint8_t gray_threshold_ = 2;
};

}  // namespace pocket::board
