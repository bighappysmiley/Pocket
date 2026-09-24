#include "pocket/canvas.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

extern "C" {
#include "qrcodegen.h"
}

namespace pocket {

namespace {

#include "font_ui.inc"

Gray ink_to_gray(uint8_t ink /*0..3*/, Gray fg) {
  if (ink == 0) return Gray::G3;  // unused by caller
  if (fg == Gray::G0) {
    // ink 3 = black, 2 = dark gray, 1 = light gray
    if (ink >= 3) return Gray::G0;
    if (ink == 2) return Gray::G1;
    return Gray::G2;
  }
  // Light on dark focus tiles: invert sense toward fg
  if (ink >= 3) return fg;
  if (ink == 2) return Gray::G2;
  return Gray::G1;
}

}  // namespace

bool Canvas::use_display_font(TextRole r) {
  switch (r) {
    case TextRole::ScreenTitle:
    case TextRole::WordMark:
    case TextRole::PinDigit:
    case TextRole::HugeClock:
      return true;
    default:
      return false;
  }
}

int Canvas::role_scale(TextRole r) {
  return r == TextRole::HugeClock ? 2 : 1;
}

int Canvas::text_height(TextRole role) const {
  const int h = use_display_font(role) ? kDisp_H : kBody_H;
  return h * role_scale(role);
}

int Canvas::text_width(std::string_view text, TextRole role) const {
  const int scale = role_scale(role);
  const bool disp = use_display_font(role);
  int w = 0;
  for (unsigned char uc : text) {
    if (uc >= 0x80) continue;
    const char c = static_cast<char>(uc);
    if (disp) w += kDisp_glyph(c).adv * scale;
    else w += kBody_glyph(c).adv * scale;
  }
  return w;
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

void Canvas::line(int x0, int y0, int x1, int y1, Gray g) {
  int dx = std::abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    set_pixel(x0, y0, g);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Canvas::draw_text(int x, int y, std::string_view text, TextRole role, Gray g) {
  const int scale = role_scale(role);
  const bool disp = use_display_font(role);
  const int native_h = disp ? kDisp_H : kBody_H;
  int cx = x;

  for (unsigned char uc : text) {
    if (uc >= 0x80) continue;
    const char c = static_cast<char>(uc);
    const uint8_t gw = disp ? kDisp_glyph(c).w : kBody_glyph(c).w;
    const uint8_t adv = disp ? kDisp_glyph(c).adv : kBody_glyph(c).adv;
    const uint8_t* bits = disp ? kDisp_glyph(c).bits : kBody_glyph(c).bits;

    if (scale == 1) {
      for (int row = 0; row < native_h; ++row) {
        for (int col = 0; col < gw; ++col) {
          const uint8_t ink = bits[row * gw + col];
          if (ink == 0) continue;
          set_pixel(cx + col, y + row, ink_to_gray(ink, g));
        }
      }
    } else {
      // Integer upscale with coverage AA for HugeClock.
      for (int oy = 0; oy < native_h * scale; ++oy) {
        for (int ox = 0; ox < gw * scale; ++ox) {
          int cover = 0;
          for (int sy = 0; sy < 2; ++sy) {
            for (int sx = 0; sx < 2; ++sx) {
              const int src_x = (ox * 2 + sx) / (scale * 2);
              const int src_y = (oy * 2 + sy) / (scale * 2);
              if (src_x >= 0 && src_x < gw && src_y >= 0 && src_y < native_h) {
                cover += bits[src_y * gw + src_x];
              }
            }
          }
          // cover 0..12 (4 samples × ink 0..3)
          if (cover <= 0) continue;
          uint8_t ink = 1;
          if (cover >= 9) ink = 3;
          else if (cover >= 5) ink = 2;
          set_pixel(cx + ox, y + oy, ink_to_gray(ink, g));
        }
      }
    }
    cx += adv * scale;
  }
}

void Canvas::draw_text_centered(int cx, int y, std::string_view text, TextRole role, Gray g) {
  const int w = text_width(text, role);
  draw_text(cx - w / 2, y, text, role, g);
}

void Canvas::draw_text_fit(int x, int y, int max_w, std::string_view text, TextRole role, Gray g) {
  if (text_width(text, role) <= max_w) {
    draw_text(x, y, text, role, g);
    return;
  }
  std::string s(text);
  const std::string ell = "...";
  while (s.size() > 1 && text_width(s + ell, role) > max_w) s.pop_back();
  draw_text(x, y, s + ell, role, g);
}

void Canvas::draw_focus_tile(int x, int y, int w, int h, std::string_view label, TextRole role) {
  fill_rect(x, y, w, h, Gray::G0);
  const int pad = 10;
  const int max_w = std::max(8, w - pad * 2);
  const int th = text_height(role);
  std::string s(label);
  const std::string ell = "...";
  if (text_width(s, role) > max_w) {
    while (s.size() > 1 && text_width(s + ell, role) > max_w) s.pop_back();
    s += ell;
  }
  const int tw = text_width(s, role);
  draw_text(x + (w - tw) / 2, y + (h - th) / 2, s, role, Gray::G3);
}

bool Canvas::draw_qr(int x, int y, int size, std::string_view payload) {
  uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
  uint8_t qr[qrcodegen_BUFFER_LEN_MAX];
  std::string data(payload);
  if (!qrcodegen_encodeText(data.c_str(), temp, qr, qrcodegen_Ecc_MEDIUM, qrcodegen_VERSION_MIN,
                            qrcodegen_VERSION_MAX, qrcodegen_Mask_AUTO, true)) {
    return false;
  }
  const int n = qrcodegen_getSize(qr);
  if (n <= 0) return false;
  const int cell = std::max(1, size / n);
  const int drawn = cell * n;
  const int ox = x + (size - drawn) / 2;
  const int oy = y + (size - drawn) / 2;
  fill_rect(ox, oy, drawn, drawn, Gray::G3);
  for (int r = 0; r < n; ++r) {
    for (int c = 0; c < n; ++c) {
      if (qrcodegen_getModule(qr, c, r)) fill_rect(ox + c * cell, oy + r * cell, cell, cell, Gray::G0);
    }
  }
  return true;
}

}  // namespace pocket
