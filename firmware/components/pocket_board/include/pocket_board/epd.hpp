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
  /** Same rotation, but only for the panel-coordinate window [px0,px1)×[py0,py1) — writes a
   * tightly packed (py1-py0) rows × (px1-px0)/8 bytes buffer. Used by present_region() so a
   * small region update costs proportional work instead of re-rotating the whole 480×800
   * canvas (384k pixel reads) just to keep a handful of changed rows. `px0`/`px1` must already
   * be byte-aligned (multiples of 8). */
  void rotate_canvas_to_mono_region(const pocket::Canvas& src, uint8_t* dst, int px0, int px1,
                                    int py0, int py1);

  bool ready_ = false;
  uint8_t* panel_1bpp_ = nullptr;
  uint8_t gray_threshold_ = 2;
};

}  // namespace pocket::board
