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

constexpr double kPi = 3.14159265358979323846;

/** Thick stroke along (x0,y0)→(x1,y1) via stepped fill_rects (fill-only, cheap). */
void thick_line(Canvas& c, int x0, int y0, int x1, int y1, int th, Gray g) {
  const int steps = std::max(6, std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
  const int hw = std::max(1, th / 2);
  for (int i = 0; i <= steps; ++i) {
    const int x = x0 + (x1 - x0) * i / steps;
    const int y = y0 + (y1 - y0) * i / steps;
    c.fill_rect(x - hw, y - hw, th, th, g);
  }
}

/**
 * Home app glyphs (v63): refined iOS-style metaphors, matched optical weight,
 * fill primitives only (no arc/stipple loops) — keeps v62 dense spacing + speed.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  // ~70% of tile — calm rim air, silhouettes a touch larger than v62.
  const int s = std::max(24, size * 70 / 100);
  const Gray bg = surface_for(g);
  const int stroke = std::max(3, s / 12);
  switch (app) {
    case HomeApp::Notes: {
      // Ruled notepad + dog-ear fold (classic Notes metaphor).
      const int w = s * 9 / 16;
      const int h = s * 3 / 4;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      const int ear = std::max(6, w / 4);
      c.fill_round_rect(x, y, w, h, s / 12, g);
      // Dog-ear: cut corner to surface, then fold triangle in ink.
      c.fill_rect(x + w - ear, y, ear, ear, bg);
      for (int i = 0; i < ear; ++i) {
        c.hline(x + w - ear, y + i, ear - i, g);
      }
      const int mx = x + w * 5 / 16;
      c.fill_rect(mx, y + stroke * 2 + ear / 4, stroke, h - stroke * 4 - ear / 4, bg);
      const int lx = mx + stroke * 2;
      const int lw = (x + w - w / 8) - lx;
      for (int i = 0; i < 3; ++i) {
        c.fill_rect(lx, y + h * (6 + i * 3) / 16, lw, stroke, bg);
      }
      break;
    }
    case HomeApp::Ledger: {
      // Ledger book: header band, three columns, bold total bar at foot.
      const int w = s * 5 / 8;
      const int h = s * 3 / 4;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, s / 12, g);
      const int head_h = std::max(stroke * 2, h / 5);
      c.fill_rect(x + stroke, y + stroke, w - stroke * 2, head_h, bg);
      const int foot_h = std::max(stroke + 1, h / 7);
      const int foot_y = y + h - stroke - foot_h;
      c.fill_rect(x + stroke, foot_y, w - stroke * 2, foot_h, bg);
      const int body_y = y + head_h + stroke + 1;
      const int body_h = foot_y - stroke - body_y;
      for (int i = 1; i <= 2; ++i) {
        c.fill_rect(x + w * i / 3 - stroke / 2, body_y, stroke, body_h, bg);
      }
      for (int r = 1; r <= 2; ++r) {
        c.fill_rect(x + stroke, body_y + body_h * r / 3 - stroke / 2, w - stroke * 2, stroke, bg);
      }
      break;
    }
    case HomeApp::Clock: {
      // Analog face — thick ring, cardinal ticks, smooth hands @ 10:10, hub.
      const int outer = s * 3 / 4;
      const int ring = std::max(3, stroke + 1);
      dot(c, cx, cy, outer, g);
      dot(c, cx, cy, outer - ring * 2, bg);
      for (int k = 0; k < 4; ++k) {
        const double ang = k * kPi / 2.0 - kPi / 2.0;
        const int r0 = outer / 2 - ring - 1;
        const int r1 = outer / 2 - 2;
        const int x0 = cx + static_cast<int>(std::lround(r0 * std::cos(ang)));
        const int y0 = cy + static_cast<int>(std::lround(r0 * std::sin(ang)));
        const int x1 = cx + static_cast<int>(std::lround(r1 * std::cos(ang)));
        const int y1 = cy + static_cast<int>(std::lround(r1 * std::sin(ang)));
        thick_line(c, x0, y0, x1, y1, stroke, g);
      }
      const int hx = cx - s / 8;
      const int hy = cy - s / 5;
      const int mx = cx + s / 4;
      const int my = cy - s / 14;
      thick_line(c, cx, cy, hx, hy, stroke + 1, g);
      thick_line(c, cx, cy, mx, my, std::max(3, stroke), g);
      dot(c, cx, cy, std::max(5, s / 8), g);
      break;
    }
    case HomeApp::Pass: {
      // ID badge — clip, rounded card, photo circle, name line, solid stripe.
      const int cw = s * 17 / 32;
      const int ch = s * 21 / 32;
      const int bx = cx - cw / 2;
      const int by = cy - ch / 2 + s / 16;
      c.fill_round_rect(cx - s / 9, by - s / 7, s * 2 / 9, s / 7, s / 18, g);
      c.fill_round_rect(bx, by, cw, ch, s / 9, g);
      const int photo = std::max(8, cw * 5 / 12);
      dot(c, cx, by + ch * 7 / 20, photo, bg);
      c.fill_rect(bx + cw / 5, by + ch * 11 / 20, cw * 3 / 5, stroke, bg);
      c.fill_rect(bx + cw / 5, by + ch * 3 / 4, cw * 3 / 5, std::max(stroke + 1, ch / 9), bg);
      break;
    }
    case HomeApp::Weather: {
      // Clear sun disc (top-right) + soft cloud below — air gap, no ray stubs.
      const int sun_cx = cx + s / 4;
      const int sun_cy = cy - s * 5 / 16;
      const int sun_d = s * 11 / 32;
      dot(c, sun_cx, sun_cy, sun_d, g);
      const int cloud_y = cy + s / 10;
      const int cloud_h = s / 4;
      c.fill_round_rect(cx - s * 3 / 8, cloud_y, s * 3 / 4, cloud_h, cloud_h / 2, g);
      dot(c, cx - s / 6, cloud_y, s * 5 / 16, g);
      dot(c, cx + s / 7, cloud_y - s / 28, s * 9 / 32, g);
      break;
    }
    case HomeApp::Music: {
      // Eighth note — oval head, thick stem, single solid pennant (no speed-line steps).
      const int head_w = s * 13 / 32;
      const int head_h = s * 9 / 32;
      const int hx = cx - s / 16;
      const int hy = cy + s / 5;
      c.fill_round_rect(hx - head_w / 2, hy - head_h / 2, head_w, head_h, head_h / 2, g);
      const int stem_w = std::max(4, s / 9);
      const int stem_x = hx + head_w / 2 - stem_w;
      const int stem_top = cy - s * 11 / 32;
      c.fill_rect(stem_x, stem_top, stem_w, hy - stem_top - head_h / 6, g);
      const int flag_h = s * 5 / 16;
      const int flag_w = s * 3 / 8;
      for (int i = 0; i < flag_h; ++i) {
        const int fw = flag_w - (flag_w * i) / flag_h;
        c.hline(stem_x + stem_w - 1, stem_top + i, std::max(3, fw), g);
      }
      break;
    }
    case HomeApp::Settings: {
      // Eight-tooth gear — radial blunt teeth + hub (fill-only, crisp at icon scale).
      const int r_body = s * 5 / 16;
      const int tooth_len = std::max(5, s / 8);
      const int tooth_w = std::max(5, s / 7);
      constexpr int kTeeth = 8;
      for (int k = 0; k < kTeeth; ++k) {
        const double ang = k * 2.0 * kPi / kTeeth;
        const double ca = std::cos(ang), sa = std::sin(ang);
        // Stretch tooth slightly along the radius for a clearer cog silhouette.
        const int tx = cx + static_cast<int>(std::lround((r_body + tooth_len / 2) * ca));
        const int ty = cy + static_cast<int>(std::lround((r_body + tooth_len / 2) * sa));
        const int tw = (std::abs(ca) > 0.7) ? tooth_len + 1 : tooth_w;
        const int th = (std::abs(sa) > 0.7) ? tooth_len + 1 : tooth_w;
        c.fill_round_rect(tx - tw / 2, ty - th / 2, tw, th, std::min(tw, th) / 3, g);
      }
      dot(c, cx, cy, r_body * 2, g);
      dot(c, cx, cy, std::max(8, s * 5 / 16), bg);
      break;
    }
    case HomeApp::Update: {
      // Circular refresh: thick ring, two gaps, bold clockwise arrow heads.
      const int outer = s * 23 / 32;
      const int ring = std::max(5, s / 7);
      dot(c, cx, cy, outer, g);
      dot(c, cx, cy, outer - ring * 2, bg);
      // Gaps at ~1 o'clock and ~7 o'clock.
      c.fill_rect(cx + outer / 8, cy - outer / 2 - 2, outer / 2, ring + 4, bg);
      c.fill_rect(cx - outer / 2 - 2, cy + outer / 12, ring + 4, outer / 2, bg);
      const int ah = std::max(9, s / 5);
      // Clockwise tips — larger L heads so they read as arrows, not gaps.
      c.fill_rect(cx + outer / 5, cy - outer / 2 - 2, ah, ring + 1, g);
      c.fill_rect(cx + outer / 5 + ah - ring - 1, cy - outer / 2 - ah / 2, ring + 1, ah, g);
      c.fill_rect(cx - outer / 2 - 2, cy + outer / 6, ring + 1, ah, g);
      c.fill_rect(cx - outer / 2 - ah / 2, cy + outer / 6 + ah - ring - 1, ah, ring + 1, g);
      break;
    }
    case HomeApp::Reading: {
      // Open book — two solid page slabs + spine crease + one rule each (clear metaphor).
      const int w = s * 7 / 8;
      const int h = s * 9 / 16;
      const int top = cy - h / 2 + s / 28;
      const int half = w / 2 - 1;
      const int r = s / 9;
      c.fill_round_rect(cx - w / 2, top, half, h, r, g);
      c.fill_round_rect(cx + 1, top, half, h, r, g);
      // Spine crease (surface color so pages read as open book, not one block).
      c.fill_rect(cx - stroke / 2, top + 1, std::max(2, stroke), h - 2, bg);
      // One text rule per page (ink on page → cut with surface).
      c.fill_rect(cx - w / 2 + half / 5, top + h / 2 - stroke / 2, half * 3 / 5, stroke, bg);
      c.fill_rect(cx + 1 + half / 5, top + h / 2 - stroke / 2, half * 3 / 5, stroke, bg);
      // Small bookmark tab at top of spine.
      const int bw = std::max(4, stroke);
      c.fill_rect(cx - bw / 2, top - s / 12, bw, s / 10, g);
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
 * v60+: content-sized rows (no stretch-to-fill air between label and next icon). */
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

void home_cell_rect(int index, int n_focus, int label_band_h, int* out_x, int* out_y, int* out_w,
                    int* out_h) {
  int grid_top = 0, cell_w = 0, cell_h = 0, icon = 0;
  home_grid_metrics(n_focus, label_band_h, &grid_top, &cell_w, &cell_h, &icon);
  const int col = index % kHomeCols;
  const int row = index / kHomeCols;
  *out_x = kSideMargin + col * (cell_w + kHomeGapX);
  *out_y = grid_top + row * (cell_h + kHomeGapY);
  *out_w = cell_w;
  *out_h = cell_h;
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

int count_home_focusable(const DeviceConfig& cfg, HomeApp* out, int cap) {
  int n = 0;
  for (int i = 0; i < kHomeGridSlots && n < cap; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg, a)) out[n++] = a;
  }
  return n;
}

}  // namespace

void App::render_home() {
  draw_status_bar();

  // Product brand — no version number on Home. Wordmark stays the bitmap font.
  canvas_.draw_text(kSideMargin, kContentTop, kProductName, Canvas::TextRole::WordMark, Gray::G0);
  const int brand_rule_y = kContentTop + canvas_.text_height(Canvas::TextRole::WordMark) + 8;
  canvas_.hline(kSideMargin, brand_rule_y, 112, Gray::G1);

  HomeApp focusable[kHomeGridSlots];
  const int n_focus = count_home_focusable(cfg_, focusable, kHomeGridSlots);
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

void App::mark_home_focus_dirty(int prev_index, int next_index) {
  ++home_focus_spin_count_;
  // Every 6th focus move: content-band partial clears residual tile ghosts.
  // Otherwise only the union of the two cells — much less panel work than ContentBand.
  if ((home_focus_spin_count_ % 6) == 0) {
    mark_content_dirty();
    return;
  }
  HomeApp focusable[kHomeGridSlots];
  const int n = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  if (n <= 0) {
    mark_content_dirty();
    return;
  }
  const int label_band_h = canvas_.text_height(Canvas::TextRole::Secondary) + 6;
  int x0 = 0, y0 = 0, w0 = 0, h0 = 0;
  int x1 = 0, y1 = 0, w1 = 0, h1 = 0;
  home_cell_rect(std::clamp(prev_index, 0, n - 1), n, label_band_h, &x0, &y0, &w0, &h0);
  home_cell_rect(std::clamp(next_index, 0, n - 1), n, label_band_h, &x1, &y1, &w1, &h1);
  const int pad = 6;
  const int rx = std::max(0, std::min(x0, x1) - pad);
  const int ry = std::max(0, std::min(y0, y1) - pad);
  const int r2 = std::min(kCanvasW, std::max(x0 + w0, x1 + w1) + pad);
  const int b2 = std::min(kCanvasH, std::max(y0 + h0, y1 + h1) + pad);
  mark_region_dirty(rx, ry, r2 - rx, b2 - ry);
}

bool App::is_home_screen() const { return nav_.current() == ScreenId::Home; }

void App::apply_home_focus_delta(int delta) {
  if (!is_home_screen() || delta == 0) return;
  HomeApp focusable[kHomeGridSlots];
  const int n = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  focus_.count = std::max(1, n);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const int prev = focus_.index;
  focus_.move(delta);
  if (focus_.index == prev) return;
  mark_home_focus_dirty(prev, focus_.index);
}

void App::handle_home(InputEvent e) {
  HomeApp focusable[kHomeGridSlots];
  const int n = count_home_focusable(cfg_, focusable, kHomeGridSlots);
  focus_.count = std::max(1, n);
  if (e == InputEvent::Up || e == InputEvent::Down) {
    apply_home_focus_delta(e == InputEvent::Up ? -1 : 1);
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
