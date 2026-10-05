#include "pocket/app.hpp"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "Update",
    "Reading", "",     "",      "",     "",        "",      "",         "",
};

void thick_hline(Canvas& c, int x, int y, int w, Gray g) {
  c.hline(x, y, w, g);
  c.hline(x, y + 1, w, g);
}
void thick_vline(Canvas& c, int x, int y, int h, Gray g) {
  c.vline(x, y, h, g);
  c.vline(x + 1, y, h, g);
}
/** Filled disc via rounded-rect with r == w/2 (square-cropped circle). */
void dot(Canvas& c, int cx, int cy, int d, Gray g) {
  d = std::max(2, d);
  c.fill_round_rect(cx - d / 2, cy - d / 2, d, d, d / 2, g);
}
/** Outline color `g` implies the surface it sits on: a white (G3) stroke means
 * we're drawing over a filled-black focus tile, so the hole must clear to G0. */
Gray surface_for(Gray g) { return g == Gray::G3 ? Gray::G0 : Gray::G3; }
void thick_line(Canvas& c, int x0, int y0, int x1, int y1, Gray g, int thickness = 2) {
  for (int t = 0; t < thickness; ++t) c.line(x0 + t, y0, x1 + t, y1, g);
}
/** Thick circular arc from angle a0 to a1 (radians, screen coords: 0 = east, +PI/2 = south). */
void arc(Canvas& c, int cx, int cy, int r, double a0, double a1, Gray g, int thickness) {
  const int steps = std::max(8, static_cast<int>(std::abs(a1 - a0) * r));
  for (int i = 0; i <= steps; ++i) {
    const double a = a0 + (a1 - a0) * i / steps;
    const double ca = std::cos(a), sa = std::sin(a);
    for (int t = 0; t < thickness; ++t) {
      const double rr = r - t;
      c.set_pixel(cx + static_cast<int>(std::lround(rr * ca)),
                  cy + static_cast<int>(std::lround(rr * sa)), g);
    }
  }
}
constexpr double kPi = 3.14159265358979323846;

/**
 * Home app glyphs (v51): solid e-ink silhouettes that fill most of `size`.
 * Consistent optical weight — filled shapes with cut-out detail, no thin
 * line-art. Every offset is proportional to `size`.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = std::max(24, size);
  const Gray bg = surface_for(g);
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  const int stroke = std::max(3, s / 12);
  switch (app) {
    case HomeApp::Notes: {
      // Document card — nearly full-height, wide, folded corner + 3 rule cuts.
      const int w = s * 4 / 5;
      const int h = s;
      const int x = cx - w / 2;
      const int y = y0;
      c.fill_round_rect(x, y, w, h, s / 9, g);
      const int fold = w / 3;
      for (int i = 0; i < fold; ++i) {
        c.hline(x + w - fold + i, y + i, fold - i, bg);
        c.vline(x + w - fold + i, y, fold - i, bg);
      }
      const int lx = x + w / 6;
      const int lw = w - w / 3;
      for (int i = 1; i <= 3; ++i) {
        c.fill_rect(lx, y + (h * (i + 1)) / 5, lw, stroke, bg);
      }
      break;
    }
    case HomeApp::Ledger: {
      // Three rising bars filling the box — clear "finance" silhouette.
      const int base_y = y0 + s - 2;
      const int bar_w = s / 4;
      const int gap = s / 12;
      const int heights[3] = {s * 45 / 100, s * 70 / 100, s * 92 / 100};
      const int total_w = 3 * bar_w + 2 * gap;
      int bx = cx - total_w / 2;
      c.fill_rect(x0 + s / 16, base_y - stroke / 2, s - s / 8, stroke, g);
      for (int i = 0; i < 3; ++i) {
        c.fill_round_rect(bx, base_y - heights[i], bar_w, heights[i], bar_w / 4, g);
        bx += bar_w + gap;
      }
      break;
    }
    case HomeApp::Clock: {
      // Full-size face + thick cut-out hands at ~10:10.
      dot(c, cx, cy, s, g);
      thick_line(c, cx, cy, cx - s / 4, cy - s / 4, bg, stroke);
      thick_line(c, cx, cy, cx + s / 3, cy - s / 8, bg, stroke);
      dot(c, cx, cy, std::max(6, s / 7), g);
      break;
    }
    case HomeApp::Pass: {
      // Wide ticket stub filling the box, side notches + tear perforations.
      const int w = s;
      const int h = s * 5 / 8;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, h / 5, g);
      const int notch = std::max(8, h / 3);
      dot(c, x, cy, notch, bg);
      dot(c, x + w, cy, notch, bg);
      const int tear_x = cx;
      for (int i = 0; i < h - 4; i += stroke * 2) {
        c.fill_rect(tear_x - 1, y + 2 + i, stroke, stroke, bg);
      }
      break;
    }
    case HomeApp::Weather: {
      // Large sun + broad cloud — cloud fills lower 2/3, sun peeks top-right.
      const int sun_d = s * 2 / 5;
      dot(c, x0 + s * 3 / 4, y0 + sun_d / 2 + 2, sun_d, g);
      const int ccy = y0 + s * 2 / 3;
      const int puff = s * 2 / 5;
      dot(c, x0 + s / 5, ccy, puff, g);
      dot(c, x0 + s / 2, ccy - s / 10, s / 2, g);
      dot(c, x0 + s * 4 / 5, ccy, puff, g);
      c.fill_round_rect(x0 + s / 12, ccy, s - s / 6, s / 4, s / 10, g);
      break;
    }
    case HomeApp::Music: {
      // Oversized eighth note — head + thick stem + solid flag fill the box.
      const int head_d = s / 2;
      const int hx = x0 + head_d / 2 + s / 12;
      const int hy = y0 + s - head_d / 2 - 2;
      dot(c, hx, hy, head_d, g);
      const int stem_w = std::max(5, s / 10);
      const int stem_x = hx + head_d / 2 - stem_w;
      const int stem_top = y0 + s / 10;
      c.fill_rect(stem_x, stem_top, stem_w, hy - stem_top - head_d / 6, g);
      const int flag_h = s / 3;
      for (int i = 0; i < flag_h; ++i) {
        const int fw = (flag_h - i) * 2 / 3 + stem_w;
        c.hline(stem_x + stem_w, stem_top + i, fw, g);
      }
      break;
    }
    case HomeApp::Settings: {
      // Chunky 8-tooth gear — teeth sit on the bounding circle, hub hollow.
      const int r_out = s / 2 - 1;
      const int r_tooth = s / 6;
      constexpr int kTeeth = 8;
      for (int k = 0; k < kTeeth; ++k) {
        const double ang = k * 2.0 * kPi / kTeeth;
        const int tx = cx + static_cast<int>(r_out * std::cos(ang));
        const int ty = cy + static_cast<int>(r_out * std::sin(ang));
        dot(c, tx, ty, 2 * r_tooth, g);
      }
      dot(c, cx, cy, s * 3 / 4, g);
      dot(c, cx, cy, s * 2 / 5, bg);
      break;
    }
    case HomeApp::Update: {
      // Thick 3/4 ring + solid arrowhead — refresh / update.
      const int r = s / 2 - s / 14;
      const int thickness = std::max(4, s / 8);
      const double a0 = -kPi / 2.0 - 0.4;
      const double a1 = kPi * 0.95;
      arc(c, cx, cy, r, a0, a1, g, thickness);
      const int hx = cx + static_cast<int>(std::lround(r * std::cos(a0)));
      const int hy = cy + static_cast<int>(std::lround(r * std::sin(a0)));
      const int hd = s / 3;
      thick_line(c, hx - hd, hy + 2, hx + 2, hy - hd + 2, g, stroke + 1);
      thick_line(c, hx - 2, hy + hd, hx + 2, hy - hd + 2, g, stroke + 1);
      break;
    }
    case HomeApp::Reading: {
      // Open book filling the box — two pages + spine + cut text rules.
      const int w = s;
      const int h = s * 4 / 5;
      const int y = cy - h / 2;
      const int half = w / 2 - 3;
      c.fill_round_rect(cx - w / 2, y, half, h, s / 9, g);
      c.fill_round_rect(cx + 3, y, half, h, s / 9, g);
      c.fill_rect(cx - stroke / 2, y, stroke, h, g);
      c.fill_rect(cx - w / 2 + half / 5, y + h / 2 - stroke / 2, half / 2, stroke, bg);
      c.fill_rect(cx + 3 + half / 5, y + h / 2 - stroke / 2, half / 2, stroke, bg);
      break;
    }
    default:
      break;
  }
}

/** Display order: real content apps, then Reading, then the always-on Settings/Update tiles —
 * keeps the utility tiles last regardless of HomeApp's underlying (bitmask-compatible) index. */
constexpr int kHomeOrder[kHomeGridSlots] = {0, 1, 2, 3, 4, 5, 8, 6, 7, 9, 10, 11, 12, 13, 14, 15};

HomeApp slot_app(int i) { return static_cast<HomeApp>(kHomeOrder[i]); }

constexpr int kHomeCols = 2;
constexpr int kHomeGapX = kSideMargin;
constexpr int kHomeGapY = 14;
/** WordMark (49) + hairline rule + air before the grid — must clear the brand. */
constexpr int kHomeBrandBand = 72;
constexpr int kHomeBottomPad = 24;
constexpr int kHomeMinTileH = 92;

/** Full-bleed metrics: tile height is derived from however many rows are
 * needed, so the grid always fills the entire area below the brand band down
 * to the bottom padding — no dead space, no fixed tile size that strands a gap. */
void home_grid_metrics(int n_focus, int* out_grid_top, int* out_tile_w, int* out_tile_h) {
  const int grid_top = kContentTop + kHomeBrandBand;
  const int rows = std::max(1, (n_focus + kHomeCols - 1) / kHomeCols);
  const int tile_w = (kCanvasW - 2 * kSideMargin - (kHomeCols - 1) * kHomeGapX) / kHomeCols;
  const int avail = kCanvasH - grid_top - kHomeBottomPad;
  const int tile_h = std::max(kHomeMinTileH, (avail - (rows - 1) * kHomeGapY) / rows);
  *out_grid_top = grid_top;
  *out_tile_w = tile_w;
  *out_tile_h = tile_h;
}

/** Word-wrap `text` into at most `max_lines` centered lines within `max_w`,
 * ellipsizing the final line if it still overflows. Draws nothing if empty. */
void draw_label_wrapped_centered(Canvas& c, int cx, int top_y, int max_w, int max_lines,
                                 const char* text, Gray g) {
  if (!text || !*text) return;
  const Canvas::TextRole role = Canvas::TextRole::Secondary;
  const int line_h = c.text_height(role) + 4;
  std::string s(text);

  // Fits on one line — common case for current labels.
  if (c.text_width(s, role) <= max_w) {
    c.draw_text_centered(cx, top_y, s, role, g);
    return;
  }

  std::vector<std::string> lines;
  size_t pos = 0;
  while (pos < s.size() && static_cast<int>(lines.size()) < max_lines) {
    size_t best = pos;
    size_t scan = pos;
    while (scan <= s.size()) {
      size_t next_space = s.find(' ', scan);
      size_t end = (next_space == std::string::npos) ? s.size() : next_space;
      if (c.text_width(s.substr(pos, end - pos), role) <= max_w) {
        best = end;
        if (end >= s.size()) break;
        scan = end + 1;
      } else {
        break;
      }
    }
    if (best == pos) {
      // Single token wider than max_w — hard cut.
      size_t cut = pos + 1;
      while (cut < s.size() && c.text_width(s.substr(pos, cut - pos), role) <= max_w) ++cut;
      best = std::max(pos + 1, cut - 1);
    }
    lines.push_back(s.substr(pos, best - pos));
    pos = best;
    while (pos < s.size() && s[pos] == ' ') ++pos;
  }
  if (pos < s.size() && !lines.empty()) {
    std::string& last = lines.back();
    std::string ell = "...";
    while (!last.empty() && c.text_width(last + ell, role) > max_w) last.pop_back();
    last += ell;
  }
  int y = top_y - (static_cast<int>(lines.size()) - 1) * line_h;
  for (const auto& line : lines) {
    c.draw_text_centered(cx, y, line, role, g);
    y += line_h;
  }
}

}  // namespace

void App::render_home() {
  draw_status_bar();

  // Product brand — no version number on Home. Wordmark stays the bitmap font.
  canvas_.draw_text(kSideMargin, kContentTop, kProductName, Canvas::TextRole::WordMark, Gray::G0);
  const int brand_rule_y = kContentTop + canvas_.text_height(Canvas::TextRole::WordMark) + 8;
  canvas_.hline(kSideMargin, brand_rule_y, 112, Gray::G1);

  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n_focus++] = a;
  }
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  int grid_top = 0, tile_w = 0, tile_h = 0;
  home_grid_metrics(n_focus, &grid_top, &tile_w, &tile_h);
  constexpr int kRadius = 22;
  // Prefer a single-line label band so the glyph can claim most of the tile.
  const int label_line_h = canvas_.text_height(Canvas::TextRole::Secondary) + 2;
  const int label_band_h = label_line_h + 8;
  const int icon_area_h = std::max(52, tile_h - label_band_h - 6);
  // Fill the tile: glyph uses nearly full icon area / tile width.
  const int glyph = std::max(52, std::min(tile_w - 18, icon_area_h));

  for (int i = 0; i < n_focus; ++i) {
    const int col = i % kHomeCols;
    const int row = i / kHomeCols;
    const int x = kSideMargin + col * (tile_w + kHomeGapX);
    const int y = grid_top + row * (tile_h + kHomeGapY);
    const HomeApp a = focusable[i];
    const bool is_focus = a == focused;
    const int label_i = static_cast<int>(a);
    const char* label =
        (label_i >= 0 && label_i < kHomeGridSlots) ? kAppLabels[label_i] : "";
    const int glyph_cy = y + 4 + icon_area_h / 2;
    const int label_top = y + tile_h - label_band_h + 4;
    const int label_max_w = tile_w - 12;

    if (is_focus) {
      canvas_.fill_round_rect(x, y, tile_w, tile_h, kRadius, Gray::G0);
      draw_app_glyph(canvas_, a, x + tile_w / 2, glyph_cy, glyph, Gray::G3);
      draw_label_wrapped_centered(canvas_, x + tile_w / 2, label_top, label_max_w, 2, label, Gray::G3);
    } else {
      canvas_.stroke_round_rect(x, y, tile_w, tile_h, kRadius, Gray::G1, 2);
      draw_app_glyph(canvas_, a, x + tile_w / 2, glyph_cy, glyph, Gray::G0);
      draw_label_wrapped_centered(canvas_, x + tile_w / 2, label_top, label_max_w, 2, label, Gray::G0);
    }
  }
}

void App::handle_home(InputEvent e) {
  HomeApp focusable[kHomeGridSlots];
  int n = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n++] = a;
  }
  focus_.count = std::max(1, n);
  if (e == InputEvent::Up || e == InputEvent::Down) {
    focus_.move(e == InputEvent::Up ? -1 : 1);
    // Full content refresh — tile region partials scrambled the Home grid on e-ink.
    mark_content_dirty();
  } else if (e == InputEvent::Select && n > 0) {
    const HomeApp launch = focusable[focus_.index];
    if (parental_requires_pin(launch) && !parental_session_unlocked_) {
      parental_pending_app_ = launch;
      parental_pin_for_app_ = true;
      pin_entry_.clear();
      pin_digit_working_ = '0';
      focus_.index = 0;
      nav_.push(ScreenId::Pin);
      after_nav();
      return;
    }
    launch_home_app(launch);
  } else if (e == InputEvent::Back) {
    // stay on home
  }
}

}  // namespace pocket
