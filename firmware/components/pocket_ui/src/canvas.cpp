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
  return r == TextRole::HugeClock ? 3 : 1;
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
    // Prefer bitmap width: AA pad made stored adv shorter than ink (smushed glyphs).
    if (disp) {
      const auto& g = kDisp_glyph(c);
      w += std::max<int>(g.w, g.adv) * scale;
    } else {
      const auto& g = kBody_glyph(c);
      w += std::max<int>(g.w, g.adv) * scale;
    }
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

void Canvas::fill_round_rect(int x, int y, int w, int h, int r, Gray g) {
  if (w <= 0 || h <= 0) return;
  r = std::max(0, std::min({r, w / 2, h / 2}));
  if (r <= 0) {
    fill_rect(x, y, w, h, g);
    return;
  }
  // Center + side bands (straight edges).
  fill_rect(x + r, y, w - 2 * r, h, g);
  fill_rect(x, y + r, r, h - 2 * r, g);
  fill_rect(x + w - r, y + r, r, h - 2 * r, g);
  // Quarter-circle corners.
  const int r2 = r * r;
  for (int dy = 0; dy < r; ++dy) {
    for (int dx = 0; dx < r; ++dx) {
      const int ox = r - 1 - dx;
      const int oy = r - 1 - dy;
      if (ox * ox + oy * oy > r2) continue;
      set_pixel(x + dx, y + dy, g);
      set_pixel(x + w - 1 - dx, y + dy, g);
      set_pixel(x + dx, y + h - 1 - dy, g);
      set_pixel(x + w - 1 - dx, y + h - 1 - dy, g);
    }
  }
}

void Canvas::stroke_round_rect(int x, int y, int w, int h, int r, Gray g, int thickness) {
  if (w <= 0 || h <= 0) return;
  thickness = std::max(1, thickness);
  r = std::max(0, std::min({r, w / 2, h / 2}));
  if (r <= 0) {
    for (int t = 0; t < thickness; ++t) {
      stroke_rect(x + t, y + t, w - 2 * t, h - 2 * t, g);
    }
    return;
  }
  // Paint outer round fill, then clear the interior with white (paper).
  fill_round_rect(x, y, w, h, r, g);
  const int inset = thickness;
  if (w > 2 * inset && h > 2 * inset) {
    const int ir = std::max(0, r - inset);
    fill_round_rect(x + inset, y + inset, w - 2 * inset, h - 2 * inset, ir, Gray::G3);
  }
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
    // Step at least by ink width so AA-padded glyphs never overlap.
    const int step = std::max<int>(gw, adv);

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
    cx += step * scale;
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

int Canvas::draw_text_wrapped(int x, int y, int max_w, int line_gap, std::string_view text, TextRole role,
                              Gray g) {
  if (text.empty()) return y;
  if (max_w < 8) max_w = 8;
  const int line_h = text_height(role) + line_gap;
  int cy = y;
  size_t i = 0;
  const size_t n = text.size();
  while (i < n) {
    while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= n) break;
    if (text[i] == '\n') {
      ++i;
      cy += line_h;
      continue;
    }
    size_t line_start = i;
    size_t last_break = i;
    size_t j = i;
    while (j < n && text[j] != '\n') {
      size_t word_end = j;
      while (word_end < n && text[word_end] != ' ' && text[word_end] != '\t' && text[word_end] != '\n') {
        ++word_end;
      }
      const std::string_view candidate = text.substr(line_start, word_end - line_start);
      if (text_width(candidate, role) <= max_w) {
        last_break = word_end;
        j = word_end;
        while (j < n && (text[j] == ' ' || text[j] == '\t')) ++j;
      } else {
        if (last_break == line_start) {
          // Single word longer than max_w — hard-split by characters.
          size_t cut = line_start + 1;
          while (cut < word_end && text_width(text.substr(line_start, cut - line_start), role) <= max_w) {
            ++cut;
          }
          if (cut > line_start + 1) --cut;
          draw_text(x, cy, text.substr(line_start, cut - line_start), role, g);
          cy += line_h;
          line_start = cut;
          last_break = cut;
          j = cut;
        } else {
          break;
        }
      }
    }
    if (last_break > line_start) {
      // Trim trailing spaces on the emitted line.
      size_t end = last_break;
      while (end > line_start && (text[end - 1] == ' ' || text[end - 1] == '\t')) --end;
      if (end > line_start) {
        draw_text(x, cy, text.substr(line_start, end - line_start), role, g);
      }
      cy += line_h;
      i = last_break;
      while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    } else if (j < n && text[j] == '\n') {
      i = j + 1;
      cy += line_h;
    } else {
      break;
    }
  }
  return cy;
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

namespace {
// Geometric wordmark: constructed capitals built from clean rectangles and angled
// bands only — no arcs, no font hinting, no pixel-stepped "8-bit" chunks. Sized to
// read clearly at the brand's fixed on-screen scale (Home, Settings, onboarding, lock).
constexpr int kWmH = 46;   // cap height
constexpr int kWmT = 9;    // stroke weight
constexpr int kWmGap = 8;  // space between letters
constexpr int kWmWP = 30;
constexpr int kWmWO = 34;
constexpr int kWmWC = 34;
constexpr int kWmWK = 30;
constexpr int kWmWE = 28;
constexpr int kWmWT = 28;
constexpr int kWmTotalW =
    kWmWP + kWmWO + kWmWC + kWmWK + kWmWE + kWmWT + 5 * kWmGap;
}  // namespace

int Canvas::pocket_wordmark_width() const { return kWmTotalW; }
int Canvas::pocket_wordmark_height() const { return kWmH; }

void Canvas::draw_pocket_wordmark(int x, int y, Gray g) {
  int cx = x;
  const int H = kWmH;
  const int T = kWmT;

  // P — full-height stem + an open rectangular bowl on the upper half.
  {
    constexpr int W = kWmWP;
    constexpr int Hb = 26;  // bowl height
    fill_rect(cx, y, T, H, g);
    stroke_round_rect(cx, y, W, Hb, 0, g, T);
    cx += W + kWmGap;
  }
  // O — a plain constructed ring; square corners keep it architectural, not a circle.
  {
    constexpr int W = kWmWO;
    stroke_round_rect(cx, y, W, H, 0, g, T);
    cx += W + kWmGap;
  }
  // C — same ring as O with a notch cut through the right wall to open the mouth.
  {
    constexpr int W = kWmWC;
    stroke_round_rect(cx, y, W, H, 0, g, T);
    constexpr int kNotchH = T + 6;
    fill_rect(cx + W - T - 2, y + H / 2 - kNotchH / 2, T + 4, kNotchH, Gray::G3);
    cx += W + kWmGap;
  }
  // K — stem + two straight angled bands meeting at mid-height (no curves).
  {
    constexpr int W = kWmWK;
    fill_rect(cx, y, T, H, g);
    const int mid = H / 2;
    for (int ry = 0; ry <= mid; ++ry) {
      const double t = static_cast<double>(mid - ry) / std::max(1, mid);
      const int xx = cx + T + static_cast<int>((W - 2 * T) * t);
      fill_rect(xx, y + ry, T, 1, g);
    }
    for (int ry = mid; ry < H; ++ry) {
      const double t = static_cast<double>(ry - mid) / std::max(1, H - 1 - mid);
      const int xx = cx + T + static_cast<int>((W - 2 * T) * t);
      fill_rect(xx, y + ry, T, 1, g);
    }
    cx += W + kWmGap;
  }
  // E — stem + three horizontal bars.
  {
    constexpr int W = kWmWE;
    fill_rect(cx, y, T, H, g);
    fill_rect(cx, y, W, T, g);
    fill_rect(cx, y + H / 2 - T / 2, W - 4, T, g);
    fill_rect(cx, y + H - T, W, T, g);
    cx += W + kWmGap;
  }
  // T — top bar + centered stem.
  {
    constexpr int W = kWmWT;
    fill_rect(cx, y, W, T, g);
    fill_rect(cx + W / 2 - T / 2, y, T, H, g);
    cx += W + kWmGap;
  }
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
