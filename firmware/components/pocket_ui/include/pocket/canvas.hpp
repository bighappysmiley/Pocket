#pragma once
#include "pocket/grayscale.hpp"
#include <cstdint>
#include <cstring>
#include <string_view>

namespace pocket {

/** Logical framebuffer 480×800, 2 bits/pixel packed (4 pixels per byte). */
class Canvas {
 public:
  void clear(Gray g = Gray::G3);
  void set_pixel(int x, int y, Gray g);
  Gray get_pixel(int x, int y) const;

  void fill_rect(int x, int y, int w, int h, Gray g);
  void stroke_rect(int x, int y, int w, int h, Gray g);
  void hline(int x, int y, int w, Gray g);
  void vline(int x, int y, int h, Gray g);

  /** Bitmap text — sizes map to Spec type scale roles (e-ink readable). */
  enum class TextRole : uint8_t {
    StatusBar,   // 12  (scale 1)
    Secondary,   // 16  (scale 2)
    Body,        // 24  (scale 3)
    ScreenTitle, // 36  (scale 4)
    WordMark,    // 48  (scale 6)
    PinDigit,    // 48  (scale 6)
    HugeClock,   // 72  (scale 9)
  };

  void draw_text(int x, int y, std::string_view text, TextRole role, Gray g);
  void draw_text_centered(int cx, int y, std::string_view text, TextRole role, Gray g);
  int text_width(std::string_view text, TextRole role) const;

  /** Inverted focus tile: fill G0, content G3 (Spec §3.3 locked). */
  void draw_focus_tile(int x, int y, int w, int h, std::string_view label, TextRole role);

  const uint8_t* pixels() const { return buf_; }
  static constexpr size_t kBufBytes = (kCanvasW * kCanvasH) / 4;

 private:
  uint8_t buf_[kBufBytes]{};
  static int role_px(TextRole r);
};

}  // namespace pocket