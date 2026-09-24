#include "pocket/canvas.hpp"
#include <algorithm>
#include <cstring>
#include <string>

extern "C" {
#include "qrcodegen.h"
}

namespace pocket {

namespace {

int glyph_index(char c) {
  if (c == ' ') return 0;
  if (c >= '0' && c <= '9') return 1 + (c - '0');
  if (c >= 'A' && c <= 'Z') return 11 + (c - 'A');
  if (c >= 'a' && c <= 'z') return 37 + (c - 'a');
  switch (c) {
    case '.':
    case '\xB7':  // middle-dot when mis-decoded
      return 63;
    case ',':
      return 64;
    case ':':
      return 65;
    case '-':
    case '\x96':  // en-dash-ish
    case '\x97':
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
    case '+':
    case '=':
    case '*':
    case '#':
    case '@':
    case '%':
    case '&':
    case '(':
    case ')':
      return 66;  // fallback dash-like for rare symbols in Wi-Fi SSIDs
    default:
      return 0;
  }
}

#include "font5x7.inc"

bool ink_at(const uint8_t* cols, int c, int r) {
  if (c < 0 || c >= 5 || r < 0 || r >= 7) return false;
  return (cols[c] & (1u << r)) != 0;
}

/** Bilinear sample of the 5×7 bitmap → soft edges under AA. */
float ink_sample(const uint8_t* cols, float fx, float fy) {
  const int x0 = static_cast<int>(fx >= 0.f ? fx : fx - 1.f);
  const int y0 = static_cast<int>(fy >= 0.f ? fy : fy - 1.f);
  const float tx = fx - static_cast<float>(x0);
  const float ty = fy - static_cast<float>(y0);
  const float v00 = ink_at(cols, x0, y0) ? 1.f : 0.f;
  const float v10 = ink_at(cols, x0 + 1, y0) ? 1.f : 0.f;
  const float v01 = ink_at(cols, x0, y0 + 1) ? 1.f : 0.f;
  const float v11 = ink_at(cols, x0 + 1, y0 + 1) ? 1.f : 0.f;
  const float a = v00 * (1.f - tx) + v10 * tx;
  const float b = v01 * (1.f - tx) + v11 * tx;
  return a * (1.f - ty) + b * ty;
}

Gray coverage_to_gray(int cover /*0..16*/, Gray fg) {
  if (cover <= 0) return Gray::G3;  // unused
  if (fg == Gray::G0) {
    // Black ink on light background — soft edges via mid grays (4×4 AA).
    if (cover >= 12) return Gray::G0;
    if (cover >= 6) return Gray::G1;
    return Gray::G2;
  }
  // Light ink on dark (focus tiles).
  if (cover >= 12) return fg;
  if (cover >= 6) return Gray::G2;
  return Gray::G1;
}

}  // namespace

int Canvas::role_px(TextRole r) {
  switch (r) {
    case TextRole::StatusBar:
      return 12;
    case TextRole::Secondary:
      return 16;
    case TextRole::Body:
      return 22;
    case TextRole::ScreenTitle:
      return 32;
    case TextRole::WordMark:
      return 40;
    case TextRole::PinDigit:
      return 40;
    case TextRole::HugeClock:
      return 64;
  }
  return 22;
}

int Canvas::role_scale(TextRole r) {
  // Compact scales for 480×800 e-ink. StatusBar stays denser (scale 2).
  switch (r) {
    case TextRole::StatusBar:
      return 2;
    case TextRole::Secondary:
      return 2;
    case TextRole::Body:
      return 2;
    case TextRole::ScreenTitle:
      return 4;
    case TextRole::WordMark:
      return 5;
    case TextRole::PinDigit:
      return 5;
    case TextRole::HugeClock:
      return 8;
  }
  return 2;
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

int Canvas::text_width(std::string_view text, TextRole role) const {
  const int scale = role_scale(role);
  return static_cast<int>(text.size()) * (5 * scale + scale);
}

void Canvas::draw_text(int x, int y, std::string_view text, TextRole role, Gray g) {
  const int scale = role_scale(role);
  int cx = x;
  for (unsigned char uc : text) {
    char c = static_cast<char>(uc);
    const int gi = glyph_index(c);
    const uint8_t* cols = kFont5x7[gi];

    if (scale == 1) {
      for (int col = 0; col < 5; ++col) {
        uint8_t bits = cols[col];
        for (int row = 0; row < 7; ++row) {
          if (bits & (1u << row)) set_pixel(cx + col, y + row, g);
        }
      }
    } else {
      // 4×4 supersampled + bilinear glyph sample → softer e-ink type.
      constexpr int kAA = 4;
      for (int oy = 0; oy < 7 * scale; ++oy) {
        for (int ox = 0; ox < 5 * scale; ++ox) {
          float sum = 0.f;
          for (int sy = 0; sy < kAA; ++sy) {
            for (int sx = 0; sx < kAA; ++sx) {
              const float fx =
                  (static_cast<float>(ox) + (static_cast<float>(sx) + 0.5f) / static_cast<float>(kAA)) /
                  static_cast<float>(scale);
              const float fy =
                  (static_cast<float>(oy) + (static_cast<float>(sy) + 0.5f) / static_cast<float>(kAA)) /
                  static_cast<float>(scale);
              sum += ink_sample(cols, fx, fy);
            }
          }
          const int cover = static_cast<int>(sum + 0.5f);
          if (cover == 0) continue;
          set_pixel(cx + ox, y + oy, coverage_to_gray(cover, g));
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
  const int pad = 12;
  const int max_w = std::max(8, w - pad * 2);
  const int th = 7 * role_scale(role);  // actual rendered glyph height
  // Fit then center the (possibly truncated) label.
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
  if (n <= 0 || size < n) return false;
  const int cell = size / n;
  const int drawn = cell * n;
  const int x0 = x + (size - drawn) / 2;
  const int y0 = y + (size - drawn) / 2;
  fill_rect(x, y, size, size, Gray::G3);
  for (int yy = 0; yy < n; ++yy) {
    for (int xx = 0; xx < n; ++xx) {
      if (qrcodegen_getModule(qr, xx, yy)) {
        fill_rect(x0 + xx * cell, y0 + yy * cell, cell, cell, Gray::G0);
      }
    }
  }
  return true;
}

}  // namespace pocket
