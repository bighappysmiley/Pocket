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
 * Home app glyphs (v60): brand-new e-ink silhouettes — not a resize of v59 line art.
 * Bold filled shapes that read at a glance on 2bpp; chrome stays the rounded square.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = std::max(22, size * 78 / 100);
  const Gray bg = surface_for(g);
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  const int stroke = std::max(2, s / 11);
  switch (app) {
    case HomeApp::Notes: {
      // Pencil over a ruled pad — pad left, thick pencil diagonal right.
      const int pad_w = s * 11 / 20;
      const int pad_h = s * 3 / 4;
      const int px = x0 + s / 10;
      const int py = cy - pad_h / 2;
      c.fill_round_rect(px, py, pad_w, pad_h, s / 12, g);
      const int lx = px + pad_w / 5;
      const int lw = pad_w - pad_w / 5 * 2;
      for (int i = 0; i < 3; ++i) {
        c.fill_rect(lx, py + pad_h * (4 + i * 3) / 16, lw, stroke, bg);
      }
      // Pencil body (parallelogram-ish via thick diagonal) + tip.
      const int p0x = x0 + s * 11 / 20;
      const int p0y = y0 + s * 3 / 4;
      const int p1x = x0 + s * 7 / 8;
      const int p1y = y0 + s / 6;
      thick_line(c, p0x, p0y, p1x, p1y, g, stroke + 2);
      // Eraser cap.
      c.fill_round_rect(p1x - stroke, p1y - stroke, stroke * 3, stroke * 3, stroke, g);
      // Tip point.
      thick_line(c, p0x, p0y, p0x - stroke, p0y + stroke, bg, stroke);
      break;
    }
    case HomeApp::Ledger: {
      // Checkmark over a receipt strip — money/tally at a glance.
      const int rw = s * 9 / 16;
      const int rh = s * 13 / 16;
      const int rx = cx - rw / 2;
      const int ry = cy - rh / 2;
      c.fill_round_rect(rx, ry, rw, rh, s / 14, g);
      // Jagged receipt top.
      for (int i = 0; i < rw; i += stroke + 1) {
        c.fill_rect(rx + i, ry, stroke, stroke, bg);
      }
      // Two amount lines.
      c.fill_rect(rx + rw / 5, ry + rh * 5 / 16, rw * 3 / 5, stroke, bg);
      c.fill_rect(rx + rw / 5, ry + rh * 8 / 16, rw * 2 / 5, stroke, bg);
      // Bold check.
      const int cx0 = rx + rw / 4;
      const int cy0 = ry + rh * 12 / 16;
      thick_line(c, cx0, cy0, cx0 + rw / 5, cy0 + rh / 10, bg, stroke + 1);
      thick_line(c, cx0 + rw / 5, cy0 + rh / 10, cx0 + rw / 2, cy0 - rh / 6, bg, stroke + 1);
      break;
    }
    case HomeApp::Clock: {
      // Bold ring + filled wedge hands (not the thin tick face).
      const int outer = s * 7 / 8;
      dot(c, cx, cy, outer, g);
      dot(c, cx, cy, outer - stroke * 2 - 2, bg);
      // Hour hand (short wedge toward 10).
      thick_line(c, cx, cy, cx - s / 7, cy - s / 6, g, stroke + 1);
      // Minute hand (longer toward 2).
      thick_line(c, cx, cy, cx + s / 4, cy - s / 14, g, stroke + 1);
      dot(c, cx, cy, std::max(5, s / 8), g);
      break;
    }
    case HomeApp::Pass: {
      // ID badge: clip + rounded card + photo square + barcode lines.
      const int cw = s * 11 / 16;
      const int ch = s * 3 / 4;
      const int bx = cx - cw / 2;
      const int by = cy - ch / 2 + s / 14;
      // Clip.
      c.fill_round_rect(cx - s / 7, by - s / 7, s * 2 / 7, s / 6, s / 18, g);
      c.fill_round_rect(bx, by, cw, ch, s / 10, g);
      // Photo block.
      c.fill_round_rect(bx + cw / 7, by + ch / 5, cw / 3, ch * 2 / 5, s / 20, bg);
      // Name lines.
      c.fill_rect(bx + cw / 2, by + ch / 4, cw * 5 / 14, stroke, bg);
      c.fill_rect(bx + cw / 2, by + ch / 4 + stroke * 2 + 1, cw / 4, stroke, bg);
      // Barcode.
      for (int i = 0; i < 5; ++i) {
        const int bw = (i % 2 == 0) ? stroke + 1 : stroke;
        c.fill_rect(bx + cw / 7 + i * (stroke + 2), by + ch * 3 / 4, bw, ch / 7, bg);
      }
      break;
    }
    case HomeApp::Weather: {
      // Umbrella — clearer weather cue than sun-behind-cloud.
      const int dome_r = s * 3 / 8;
      const int dome_cy = cy - s / 14;
      // Dome as thick upper arc.
      arc(c, cx, dome_cy, dome_r, kPi, 2.0 * kPi, g, stroke + 2);
      c.fill_round_rect(cx - dome_r, dome_cy - stroke, dome_r * 2, stroke + 1, 1, g);
      // Shaft + crook handle.
      c.fill_rect(cx - stroke / 2, dome_cy, stroke, s * 5 / 14, g);
      arc(c, cx + s / 10, cy + s * 5 / 16, s / 8, 0, kPi, g, stroke);
      break;
    }
    case HomeApp::Music: {
      // Over-ear headphones — solid cups + headband.
      const int band_r = s * 5 / 14;
      arc(c, cx, cy - s / 14, band_r, kPi + 0.25, 2.0 * kPi - 0.25, g, stroke + 1);
      const int cup_w = s / 4;
      const int cup_h = s * 5 / 14;
      c.fill_round_rect(cx - band_r - cup_w / 4, cy - cup_h / 4, cup_w, cup_h, cup_w / 3, g);
      c.fill_round_rect(cx + band_r - cup_w * 3 / 4, cy - cup_h / 4, cup_w, cup_h, cup_w / 3, g);
      // Inner cup hollows.
      c.fill_round_rect(cx - band_r + stroke / 2, cy - cup_h / 6, cup_w / 2, cup_h * 2 / 3, cup_w / 4, bg);
      c.fill_round_rect(cx + band_r - cup_w / 2, cy - cup_h / 6, cup_w / 2, cup_h * 2 / 3, cup_w / 4, bg);
      break;
    }
    case HomeApp::Settings: {
      // Three slider rows with knobs — settings, not a gear.
      const int row_h = s / 5;
      const int track_w = s * 3 / 4;
      const int track_x = cx - track_w / 2;
      const int knob = std::max(8, s / 5);
      const int positions[3] = {track_w / 4, track_w * 2 / 3, track_w / 2};
      for (int i = 0; i < 3; ++i) {
        const int ty = y0 + s / 6 + i * (row_h + s / 10);
        c.fill_round_rect(track_x, ty + row_h / 3, track_w, std::max(3, row_h / 3), 2, g);
        dot(c, track_x + positions[i], ty + row_h / 2, knob, g);
        dot(c, track_x + positions[i], ty + row_h / 2, knob / 2, bg);
      }
      break;
    }
    case HomeApp::Update: {
      // Download tray: bold down-arrow into a dock (OTA metaphor).
      const int shaft_w = std::max(5, s / 7);
      const int shaft_h = s * 5 / 14;
      c.fill_rect(cx - shaft_w / 2, y0 + s / 8, shaft_w, shaft_h, g);
      // Arrowhead.
      const int head_y = y0 + s / 8 + shaft_h - 2;
      const int head_w = s * 5 / 14;
      for (int i = 0; i < head_w / 2; ++i) {
        c.hline(cx - head_w / 2 + i, head_y + i, head_w - 2 * i, g);
      }
      // Tray / dock.
      const int tray_y = y0 + s * 3 / 4;
      c.fill_rect(x0 + s / 6, tray_y, s * 2 / 3, stroke + 1, g);
      c.fill_rect(x0 + s / 6, tray_y - s / 8, stroke + 1, s / 8, g);
      c.fill_rect(x0 + s * 5 / 6 - stroke - 1, tray_y - s / 8, stroke + 1, s / 8, g);
      break;
    }
    case HomeApp::Reading: {
      // Closed hardcover + bookmark ribbon peeking from the fore-edge.
      const int w = s * 5 / 8;
      const int h = s * 3 / 4;
      const int x = cx - w / 2 - s / 20;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, s / 12, g);
      // Binding stripe.
      c.fill_rect(x + w / 5, y + stroke, std::max(2, stroke), h - stroke * 2, bg);
      // Cover title rules.
      c.fill_rect(x + w * 2 / 5, y + h * 5 / 16, w * 2 / 5, stroke, bg);
      c.fill_rect(x + w * 2 / 5, y + h * 5 / 16 + stroke * 2 + 1, w / 4, stroke, bg);
      // Bookmark ribbon — solid strip hanging past the bottom edge.
      const int bm_w = std::max(4, s / 9);
      const int bm_x = x + w - bm_w - s / 14;
      c.fill_rect(bm_x, y + h * 2 / 3, bm_w, h / 3 + s / 10, g);
      // Forked tip.
      c.fill_rect(bm_x, y + h + s / 10 - stroke, bm_w, stroke, bg);
      c.fill_rect(bm_x + bm_w / 2 - 1, y + h + s / 14, 2, stroke + 1, bg);
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

/** iPhone-style Home: 3 columns of square rounded icons with labels underneath.
 * v60: content-sized rows (no stretch-to-fill air between label and next icon). */
constexpr int kHomeCols = 3;
constexpr int kHomeGapX = 16;
constexpr int kHomeGapY = 8;
/** WordMark (49) + hairline rule + air before the grid — must clear the brand. */
constexpr int kHomeBrandBand = 56;
constexpr int kHomeBottomPad = 16;
/** Cap so icons stay calm — slight size from v59 OK. */
constexpr int kHomeIconMax = 100;
constexpr int kHomeIconMin = 68;

/** Cell metrics for an iOS-style grid: square icon + label band under it. */
void home_grid_metrics(int n_focus, int label_band_h, int* out_grid_top, int* out_cell_w,
                       int* out_cell_h, int* out_icon) {
  const int grid_top_min = kContentTop + kHomeBrandBand;
  const int rows = std::max(1, (n_focus + kHomeCols - 1) / kHomeCols);
  const int cell_w = (kCanvasW - 2 * kSideMargin - (kHomeCols - 1) * kHomeGapX) / kHomeCols;
  // Size icon from column width — do NOT stretch cell_h to fill leftover canvas height
  // (that was the loose vertical rhythm on v59).
  int icon = std::min(cell_w - 4, kHomeIconMax);
  icon = std::max(kHomeIconMin, icon);
  const int cell_h = icon + label_band_h + 4;
  const int grid_h = rows * cell_h + (rows - 1) * kHomeGapY;
  const int avail = kCanvasH - grid_top_min - kHomeBottomPad;
  // Pack from the top; only a little leftover air above the grid (never between rows).
  int grid_top = grid_top_min;
  if (avail > grid_h) {
    grid_top = grid_top_min + std::min(20, (avail - grid_h) / 6);
  }
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
  const int label_band_h = label_line_h + 4;
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
    const int icon_y = cell_y;
    const int glyph_cx = icon_x + icon / 2;
    const int glyph_cy = icon_y + icon / 2;
    const int label_top = icon_y + icon + 4;
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
      pin_spin_count_ = 0;
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
