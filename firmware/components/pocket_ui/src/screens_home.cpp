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
 * Home app glyphs (v59): iOS-style icons — clear silhouettes with one consistent
 * optical weight inside rounded squares. Slightly larger than v58's inset glyphs,
 * still far from the old fill-the-tile monsters.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  // ~80% of the icon well (v58 used 72%) — a tiny bump in presence.
  const int s = std::max(22, size * 80 / 100);
  const Gray bg = surface_for(g);
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  const int stroke = std::max(2, s / 12);
  switch (app) {
    case HomeApp::Notes: {
      // Document with folded corner + three rule lines.
      const int w = s * 5 / 8;
      const int h = s * 13 / 16;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, s / 9, g);
      const int fold = w * 2 / 5;
      for (int i = 0; i < fold; ++i) {
        c.hline(x + w - fold + i, y + i, fold - i, bg);
        c.vline(x + w - fold + i, y, fold - i, bg);
      }
      const int lx = x + w / 5;
      const int lw = w - w / 5 * 2;
      for (int i = 0; i < 3; ++i) {
        const int ly = y + h * (5 + i * 3) / 16;
        c.fill_rect(lx, ly, lw - (i == 2 ? lw / 4 : 0), stroke, bg);
      }
      break;
    }
    case HomeApp::Ledger: {
      // Three ascending bars on a baseline — finance at a glance.
      const int base_y = y0 + s - s / 8;
      const int bar_w = std::max(5, s / 5);
      const int gap = std::max(3, s / 9);
      const int heights[3] = {s * 36 / 100, s * 56 / 100, s * 76 / 100};
      const int total_w = 3 * bar_w + 2 * gap;
      int bx = cx - total_w / 2;
      c.fill_rect(bx - 2, base_y - stroke / 2, total_w + 4, stroke, g);
      for (int i = 0; i < 3; ++i) {
        c.fill_round_rect(bx, base_y - heights[i], bar_w, heights[i], bar_w / 3, g);
        bx += bar_w + gap;
      }
      break;
    }
    case HomeApp::Clock: {
      // Round face, cardinal ticks, hands at ~10:10.
      const int face = s * 7 / 8;
      dot(c, cx, cy, face, g);
      for (int k = 0; k < 4; ++k) {
        const double ang = k * kPi / 2.0;
        const int r0 = face / 2 - stroke - 1;
        const int r1 = face / 2 - 2;
        thick_line(c, cx + static_cast<int>(std::lround(r0 * std::cos(ang))),
                   cy + static_cast<int>(std::lround(r0 * std::sin(ang))),
                   cx + static_cast<int>(std::lround(r1 * std::cos(ang))),
                   cy + static_cast<int>(std::lround(r1 * std::sin(ang))), bg, stroke);
      }
      thick_line(c, cx, cy, cx - s / 6, cy - s / 5, bg, stroke);
      thick_line(c, cx, cy, cx + s / 5, cy - s / 12, bg, stroke);
      dot(c, cx, cy, std::max(4, s / 9), g);
      break;
    }
    case HomeApp::Pass: {
      // Ticket with side notches + dashed tear.
      const int w = s * 7 / 8;
      const int h = s * 1 / 2;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, h / 4, g);
      const int notch = std::max(6, h / 3);
      dot(c, x, cy, notch, bg);
      dot(c, x + w, cy, notch, bg);
      for (int i = 2; i < h - 4; i += stroke * 2 + 1) {
        c.fill_rect(cx - 1, y + i, stroke, stroke, bg);
      }
      break;
    }
    case HomeApp::Weather: {
      // Sun peeking from behind a cloud.
      const int sun_d = s * 5 / 14;
      const int sun_cx = x0 + s * 3 / 4;
      const int sun_cy = y0 + sun_d / 2 + s / 10;
      dot(c, sun_cx, sun_cy, sun_d, g);
      for (int k = 0; k < 4; ++k) {
        const double ang = -kPi / 2.0 + k * 0.5;
        const int sx = sun_cx + static_cast<int>(std::lround((sun_d / 2 + 1) * std::cos(ang)));
        const int sy = sun_cy + static_cast<int>(std::lround((sun_d / 2 + 1) * std::sin(ang)));
        const int ex = sun_cx + static_cast<int>(std::lround((sun_d / 2 + s / 7) * std::cos(ang)));
        const int ey = sun_cy + static_cast<int>(std::lround((sun_d / 2 + s / 7) * std::sin(ang)));
        thick_line(c, sx, sy, ex, ey, g, stroke);
      }
      const int ccy = y0 + s * 5 / 8;
      const int puff = s * 3 / 10;
      dot(c, x0 + s / 4, ccy, puff, g);
      dot(c, cx, ccy - s / 12, s * 2 / 5, g);
      dot(c, x0 + s * 3 / 4, ccy, puff, g);
      c.fill_round_rect(x0 + s / 8, ccy, s - s / 4, s / 5, s / 12, g);
      break;
    }
    case HomeApp::Music: {
      // Eighth note — solid head, stem, pennant.
      const int head_d = s * 2 / 5;
      const int hx = x0 + s / 4 + head_d / 4;
      const int hy = y0 + s - head_d / 2 - s / 10;
      dot(c, hx, hy, head_d, g);
      const int stem_w = std::max(3, s / 11);
      const int stem_x = hx + head_d / 2 - stem_w;
      const int stem_top = y0 + s / 8;
      c.fill_rect(stem_x, stem_top, stem_w, hy - stem_top - head_d / 5, g);
      const int flag_h = s / 4;
      for (int i = 0; i < flag_h; ++i) {
        const int fw = (flag_h - i) * 3 / 5 + stem_w;
        c.hline(stem_x + stem_w, stem_top + i, fw, g);
      }
      break;
    }
    case HomeApp::Settings: {
      // Six-tooth gear with clear hub.
      const int r_out = s * 2 / 5;
      const int r_tooth = std::max(3, s / 8);
      constexpr int kTeeth = 6;
      for (int k = 0; k < kTeeth; ++k) {
        const double ang = k * 2.0 * kPi / kTeeth + kPi / 6.0;
        dot(c, cx + static_cast<int>(r_out * std::cos(ang)),
            cy + static_cast<int>(r_out * std::sin(ang)), 2 * r_tooth, g);
      }
      dot(c, cx, cy, s * 3 / 5, g);
      dot(c, cx, cy, s * 1 / 3, bg);
      break;
    }
    case HomeApp::Update: {
      // Refresh arc with filled arrowhead.
      const int r = s * 2 / 5;
      const int thickness = std::max(3, s / 9);
      const double a0 = -kPi / 2.0 - 0.35;
      const double a1 = kPi * 0.9;
      arc(c, cx, cy, r, a0, a1, g, thickness);
      const int hx = cx + static_cast<int>(std::lround(r * std::cos(a0)));
      const int hy = cy + static_cast<int>(std::lround(r * std::sin(a0)));
      const int hd = s / 4;
      thick_line(c, hx - hd, hy + 1, hx + 1, hy - hd + 1, g, stroke + 1);
      thick_line(c, hx - 1, hy + hd - 1, hx + 1, hy - hd + 1, g, stroke + 1);
      break;
    }
    case HomeApp::Reading: {
      // Open book — two pages + spine + mid-line marks.
      const int w = s * 7 / 8;
      const int h = s * 2 / 3;
      const int y = cy - h / 2;
      const int half = w / 2 - 2;
      c.fill_round_rect(cx - w / 2, y, half, h, s / 10, g);
      c.fill_round_rect(cx + 2, y, half, h, s / 10, g);
      c.fill_rect(cx - stroke / 2, y, stroke, h, g);
      c.fill_rect(cx - w / 2 + half / 5, y + h / 2 - stroke / 2, half / 2, stroke, bg);
      c.fill_rect(cx + 2 + half / 5, y + h / 2 - stroke / 2, half / 2, stroke, bg);
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

/** iPhone-style Home: 3 columns of square rounded icons with labels underneath. */
constexpr int kHomeCols = 3;
constexpr int kHomeGapX = 18;
constexpr int kHomeGapY = 14;
/** WordMark (49) + hairline rule + air before the grid — must clear the brand. */
constexpr int kHomeBrandBand = 64;
constexpr int kHomeBottomPad = 24;
/** Cap so icons stay calm — a touch above v58 glyphs, not old oversized tiles. */
constexpr int kHomeIconMax = 100;
constexpr int kHomeIconMin = 64;

/** Cell metrics for an iOS-style grid: square icon + label band under it. */
void home_grid_metrics(int n_focus, int label_band_h, int* out_grid_top, int* out_cell_w,
                       int* out_cell_h, int* out_icon) {
  const int grid_top = kContentTop + kHomeBrandBand;
  const int rows = std::max(1, (n_focus + kHomeCols - 1) / kHomeCols);
  const int cell_w = (kCanvasW - 2 * kSideMargin - (kHomeCols - 1) * kHomeGapX) / kHomeCols;
  const int avail = kCanvasH - grid_top - kHomeBottomPad;
  const int cell_h = std::max(kHomeIconMin + label_band_h + 8,
                              (avail - (rows - 1) * kHomeGapY) / rows);
  // Square icon: nearly fill the cell width (iOS-style plates), leave label air.
  int icon = std::min(cell_w - 4, cell_h - label_band_h - 8);
  icon = std::max(kHomeIconMin, std::min(kHomeIconMax, icon));
  *out_grid_top = grid_top;
  *out_cell_w = cell_w;
  *out_cell_h = cell_h;
  *out_icon = icon;
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

  const int label_line_h = canvas_.text_height(Canvas::TextRole::Secondary) + 2;
  const int label_band_h = label_line_h + 8;
  int grid_top = 0, cell_w = 0, cell_h = 0, icon = 0;
  home_grid_metrics(n_focus, label_band_h, &grid_top, &cell_w, &cell_h, &icon);
  // iOS corner radius ≈ 22% of icon edge.
  const int radius = std::max(12, icon * 22 / 100);

  for (int i = 0; i < n_focus; ++i) {
    const int col = i % kHomeCols;
    const int row = i / kHomeCols;
    const int cell_x = kSideMargin + col * (cell_w + kHomeGapX);
    const int cell_y = grid_top + row * (cell_h + kHomeGapY);
    const HomeApp a = focusable[i];
    const bool is_focus = a == focused;
    const int label_i = static_cast<int>(a);
    const char* label =
        (label_i >= 0 && label_i < kHomeGridSlots) ? kAppLabels[label_i] : "";

    // Square icon centered in the cell width; label band sits under it.
    const int icon_x = cell_x + (cell_w - icon) / 2;
    const int icon_y = cell_y + 4;
    const int glyph_cx = icon_x + icon / 2;
    const int glyph_cy = icon_y + icon / 2;
    const int label_top = icon_y + icon + 6;
    const int label_max_w = cell_w - 4;

    if (is_focus) {
      canvas_.fill_round_rect(icon_x, icon_y, icon, icon, radius, Gray::G0);
      draw_app_glyph(canvas_, a, glyph_cx, glyph_cy, icon, Gray::G3);
    } else {
      // Soft filled square (app-icon plate) + calm outline.
      canvas_.fill_round_rect(icon_x, icon_y, icon, icon, radius, Gray::G2);
      canvas_.stroke_round_rect(icon_x, icon_y, icon, icon, radius, Gray::G1, 1, Gray::G2);
      draw_app_glyph(canvas_, a, glyph_cx, glyph_cy, icon, Gray::G0);
    }
    // Name centered under the icon — always ink on paper (iPhone-style caption).
    draw_label_wrapped_centered(canvas_, cell_x + cell_w / 2, label_top, label_max_w, 1, label,
                                Gray::G0);
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
