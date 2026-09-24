#pragma once
#include "pocket/canvas.hpp"
#include "pocket/refresh.hpp"
#include <cstdint>

namespace pocket::board {

class EpdDisplay {
 public:
  bool init();
  void present(const pocket::Canvas& canvas, pocket::RefreshMode mode);
  void sleep();

 private:
  void rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst);

  bool ready_ = false;
  uint8_t* panel_1bpp_ = nullptr;
};

}  // namespace pocket::board
