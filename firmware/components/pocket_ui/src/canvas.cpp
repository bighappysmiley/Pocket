#include "pocket/canvas.hpp"
#include <algorithm>
#include <cstring>

namespace pocket {

namespace {

int glyph_index(char c) {
  if (c == ' ') return 0;
  if (c >= '0' && c <= '9') return 1 + (c - '0');
  if (c >= 'A' && c <= 'Z') return 11 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 37 + (c - 'a');
  switch (c) {
    case '.':
      return 63;
    case ',':
      return 64;
    case ':':
      return 65;
    case '-':
      return 66;
    case '\'':
      return 67;
    case '"':
      return 68;
    case '?':
      return 69;
    case '!':
      return 70;
    case '/':
      return 71;
    default:
      return 0;
  }
}

#include "font5x7.inc"

}  // namespace

int Canvas::role_px(TextRole r) {
  // Pixel height ≈ 7 * (role_px/8). Keep Body and ScreenTitle on different scales
  // so titles read as titles on the 480x800 panel (was both scale-2 before).
  switch (r) {
    case TextRole::StatusBar:
      return 12;
    case TextRole::Secondary:
      return 16;
    case TextRole::Body:
      return 24;
    case TextRole::ScreenTitle:
      return 36;
    case TextRole::WordMark:
      return 48;
    case TextRole::PinDigit:
      return 48;
    case TextRole::HugeClock:
      return 72;
  }
  return 24;
}

void Canvas::clear(Gray g) {
  const uint8_t v = static_cast<uint8_t>(g) & 0x3;
  const uint8_t byte = static_cast<uint8_t>((v << 6) | (v << 4) | (v << 2) | v);
  std::memset(buf_, byte, kBufBytes);
}

void Canvas::set_pixel(int x, int y, Gray g) {
  if (x < 0 || y < 0 || x >= kCanvasW || y >= kCanvasH) return;
  const size_t i = static_cast<size_t>(y * kCanvasW + x);
  const size_t bi = i / 4;
  const int shift = 6 - static_cast<int>((i % 4) * 2);
  buf_[bi] = static_cast<uint8_t>((buf_[bi] & ~(0x3 << shift)) | ((static_cast<uint8_t>(g) & 0x3) << shift));
}

Gray Canvas::get_pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= kCanvasW || y >= kCanvasH) return Gray::G3;
  const size_t i = static_cast<size_t>(y * kCanvasW + x);
  const size_t bi = i / 4;
  const int shift = 6 - static_cast<int>((i % 4) * 2);
  return static_cast<Gray>((buf_[bi] >> shift) & 0x3);
}

void Canvas::fill_rect(int x, int y, int w, int h, Gray g) {
  for (int yy = y; yy < y + h; ++yy)
    for (int xx = x; xx < x + w; ++xx) set_pixel(xx, yy, g);
}

void Canvas::stroke_rect(int x, int y, int w, int h, Gray g) {
  hline(x, y, w, g);
  hline(x, y + h - 1, w, g);
  vline(x, y, h, g);
  vline(x + w - 1, y, h, g);
}

void Canvas::hline(int x, int y, int w, Gray g) {
  for (int i = 0; i < w; ++i) set_pixel(x + i, y, g);
}

void Canvas::vline(int x, int y, int h, Gray g) {
  for (int i = 0; i < h; ++i) set_pixel(x, y + i, g);
}

int Canvas::text_width(std::string_view text, TextRole role) const {
  const int scale = std::max(1, role_px(role) / 8);
  return static_cast<int>(text.size()) * (5 * scale + scale);
}

void Canvas::draw_text(int x, int y, std::string_view text, TextRole role, Gray g) {
  const int scale = std::max(1, role_px(role) / 8);
  int cx = x;
  for (unsigned char uc : text) {
    char c = static_cast<char>(uc);
    const int gi = glyph_index(c);
    const uint8_t* cols = kFont5x7[gi];
    for (int col = 0; col < 5; ++col) {
      uint8_t bits = cols[col];
      for (int row = 0; row < 7; ++row) {
        if (bits & (1u << row)) {
          for (int sy = 0; sy < scale; ++sy)
            for (int sx = 0; sx < scale; ++sx)
              set_pixel(cx + col * scale + sx, y + row * scale + sy, g);
        }
      }
    }
    cx += 5 * scale + scale;
  }
}

void Canvas::draw_text_centered(int cx, int y, std::string_view text, TextRole role, Gray g) {
  const int w = text_width(text, role);
  draw_text(cx - w / 2, y, text, role, g);
}

void Canvas::draw_focus_tile(int x, int y, int w, int h, std::string_view label, TextRole role) {
  fill_rect(x, y, w, h, Gray::G0);
  const int tw = text_width(label, role);
  const int th = role_px(role);
  draw_text(x + (w - tw) / 2, y + (h - th) / 2, label, role, Gray::G3);
}

}  // namespace pocket