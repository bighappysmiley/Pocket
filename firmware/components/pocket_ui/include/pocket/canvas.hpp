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
  void line(int x0, int y0, int x1, int y1, Gray g);

  /** Proportional DejaVu UI type (normal font, readable size). */
  enum class TextRole : uint8_t {
    StatusBar,    // 28px body
    Secondary,    // 28px body
    Body,         // 28px body
    ScreenTitle,  // 42px display
    WordMark,     // 42px display
    PinDigit,     // 42px display
    HugeClock,    // 42px display ×3
  };

  void draw_text(int x, int y, std::string_view text, TextRole role, Gray g);
  void draw_text_centered(int cx, int y, std::string_view text, TextRole role, Gray g);
  void draw_text_fit(int x, int y, int max_w, std::string_view text, TextRole role, Gray g);
  int text_width(std::string_view text, TextRole role) const;
  int text_height(TextRole role) const;

  void draw_focus_tile(int x, int y, int w, int h, std::string_view label, TextRole role);
  bool draw_qr(int x, int y, int size, std::string_view payload);

  const uint8_t* pixels() const { return buf_; }
  static constexpr size_t kBufBytes = (kCanvasW * kCanvasH) / 4;

 private:
  uint8_t buf_[kBufBytes]{};
  static bool use_display_font(TextRole r);
  static int role_scale(TextRole r);
};

}  // namespace pocket
