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
  /** Bresenham line for abstract lock motifs. */
  void line(int x0, int y0, int x1, int y1, Gray g);

  /** Bitmap text — sizes map to Spec type scale roles (e-ink readable). */
  enum class TextRole : uint8_t {
    StatusBar,   // ~14px  (scale 2, 4×4 AA)
    Secondary,   // ~14px  (scale 2)
    Body,        // ~21px  (scale 3)
    ScreenTitle, // ~28px  (scale 4)
    WordMark,    // ~35px  (scale 5)
    PinDigit,    // ~35px  (scale 5)
    HugeClock,   // ~56px  (scale 8)
  };

  void draw_text(int x, int y, std::string_view text, TextRole role, Gray g);
  void draw_text_centered(int cx, int y, std::string_view text, TextRole role, Gray g);
  /** Draw text clipped to max_w; appends "..." if truncated. */
  void draw_text_fit(int x, int y, int max_w, std::string_view text, TextRole role, Gray g);
  int text_width(std::string_view text, TextRole role) const;

  /** Inverted focus tile: fill G0, label G3, truncates label to fit. */
  void draw_focus_tile(int x, int y, int w, int h, std::string_view label, TextRole role);

  /** Draw a QR for `payload` centered in the square (x,y,size). */
  bool draw_qr(int x, int y, int size, std::string_view payload);

  const uint8_t* pixels() const { return buf_; }
  static constexpr size_t kBufBytes = (kCanvasW * kCanvasH) / 4;

 private:
  uint8_t buf_[kBufBytes]{};
  static int role_px(TextRole r);
  static int role_scale(TextRole r);
};

}  // namespace pocket
