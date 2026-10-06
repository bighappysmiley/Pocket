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

/** Solid triangular arrowhead; `ang` is the direction the tip points (screen radians). */
void arrowhead(Canvas& c, int tip_x, int tip_y, double ang, int len, Gray g) {
  const double left = ang + 2.4;
  const double right = ang - 2.4;
  const int lx = tip_x + static_cast<int>(std::lround(len * std::cos(left)));
  const int ly = tip_y + static_cast<int>(std::lround(len * std::sin(left)));
  const int rx = tip_x + static_cast<int>(std::lround(len * std::cos(right)));
  const int ry = tip_y + static_cast<int>(std::lround(len * std::sin(right)));
  for (int i = 0; i <= len; ++i) {
    const double t = static_cast<double>(i) / std::max(1, len);
    const int ax = tip_x + static_cast<int>(std::lround(t * (lx - tip_x)));
    const int ay = tip_y + static_cast<int>(std::lround(t * (ly - tip_y)));
    const int dx = tip_x + static_cast<int>(std::lround(t * (rx - tip_x)));
    const int dy = tip_y + static_cast<int>(std::lround(t * (ry - tip_y)));
    c.line(ax, ay, dx, dy, g);
    c.line(ax, ay + 1, dx, dy + 1, g);
  }
}

/**
 * Home app glyphs (v61): designed e-ink silhouettes — classic metaphors,
 * consistent optical weight and padding inside the iPhone-style squircle.
 * High-contrast filled / 2-weight shapes that read at ~48–64px.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  // ~70% of tile edge — consistent rim padding across the set.
  const int s = std::max(24, size * 70 / 100);
  const Gray bg = surface_for(g);
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  const int stroke = std::max(3, s / 11);
  switch (app) {
    case HomeApp::Notes: {
      // Lined pad — solid body, left margin rule, three even horizontal rules.
      const int w = s * 5 / 8;
      const int h = s * 13 / 16;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, s / 10, g);
      c.fill_rect(x + w / 5, y + stroke * 2, stroke, h - stroke * 4, bg);
      const int lx = x + w / 5 + stroke * 2;
      const int lw = w - (lx - x) - w / 7;
      for (int i = 0; i < 3; ++i) {
        c.fill_rect(lx, y + h * (4 + i * 3) / 16, lw, stroke, bg);
      }
      break;
    }
    case HomeApp::Ledger: {
      // Three bold columns under a header — accounting ledger, not a grid scribble.
      const int w = s * 11 / 16;
      const int h = s * 13 / 16;
      const int x = cx - w / 2;
      const int y = cy - h / 2;
      c.fill_round_rect(x, y, w, h, s / 11, g);
      const int head_h = h / 4;
      c.fill_rect(x + stroke, y + stroke, w - stroke * 2, head_h - stroke, bg);
      const int body_y = y + head_h + 1;
      const int body_h = y + h - stroke - body_y;
      // Two vertical dividers → three columns.
      for (int i = 1; i <= 2; ++i) {
        c.fill_rect(x + w * i / 3 - stroke / 2, body_y, stroke, body_h, bg);
      }
      // Two horizontal rules spanning the body (row structure).
      for (int r = 1; r <= 2; ++r) {
        c.fill_rect(x + stroke, body_y + body_h * r / 3 - stroke / 2, w - stroke * 2, stroke, bg);
      }
      break;
    }
    case HomeApp::Clock: {
      // Analog face: thick ring, 12/3/6/9 ticks, hands at 10:10, hub.
      const int outer = s * 13 / 16;
      dot(c, cx, cy, outer, g);
      dot(c, cx, cy, outer - stroke * 2 - 2, bg);
      for (int k = 0; k < 4; ++k) {
        const double ang = k * kPi / 2.0 - kPi / 2.0;  // start at 12
        const int r0 = outer / 2 - stroke - 3;
        const int r1 = outer / 2 - 2;
        thick_line(c,
                   cx + static_cast<int>(std::lround(r0 * std::cos(ang))),
                   cy + static_cast<int>(std::lround(r0 * std::sin(ang))),
                   cx + static_cast<int>(std::lround(r1 * std::cos(ang))),
                   cy + static_cast<int>(std::lround(r1 * std::sin(ang))), g, stroke);
      }
      thick_line(c, cx, cy, cx - s / 8, cy - s / 5, g, stroke + 1);
      thick_line(c, cx, cy, cx + s / 4, cy - s / 14, g, stroke + 1);
      dot(c, cx, cy, std::max(5, s / 8), g);
      break;
    }
    case HomeApp::Pass: {
      // Solid pass card + clip — filled silhouette, one photo cutout, short barcode.
      const int cw = s * 9 / 16;
      const int ch = s * 11 / 16;
      const int bx = cx - cw / 2;
      const int by = cy - ch / 2 + s / 12;
      c.fill_round_rect(cx - s / 7, by - s / 6, s * 2 / 7, s / 5, s / 14, g);
      c.fill_round_rect(bx, by, cw, ch, s / 9, g);
      // Small photo window (keeps the card mostly solid ink).
      c.fill_round_rect(bx + cw / 4, by + ch / 5, cw / 2, ch / 4, s / 20, bg);
      c.fill_rect(bx + cw / 4, by + ch * 9 / 16, cw / 2, stroke, bg);
      // Three barcode bars.
      for (int i = 0; i < 3; ++i) {
        c.fill_rect(bx + cw / 4 + i * (stroke + 3), by + ch * 3 / 4, stroke + (i & 1), ch / 9, bg);
      }
      break;
    }
    case HomeApp::Weather: {
      // Sun disc (no spiked rays) peeking over a smooth three-puff cloud.
      const int sun_cx = x0 + s * 3 / 4;
      const int sun_cy = y0 + s * 9 / 32;
      const int sun_d = s * 3 / 8;
      // Four stubby ray blobs (dots) — readable, not a jagged mohawk.
      for (int k = 0; k < 4; ++k) {
        const double ang = -kPi * 0.65 + k * 0.4;
        const int rr = sun_d / 2 + s / 10;
        dot(c,
            sun_cx + static_cast<int>(std::lround(rr * std::cos(ang))),
            sun_cy + static_cast<int>(std::lround(rr * std::sin(ang))),
            std::max(4, s / 10), g);
      }
      dot(c, sun_cx, sun_cy, sun_d, g);
      const int ccy = cy + s / 8;
      dot(c, cx - s / 4, ccy, s * 3 / 8, g);
      dot(c, cx + s / 8, ccy - s / 12, s * 7 / 16, g);
      dot(c, cx + s / 3, ccy + s / 24, s * 5 / 16, g);
      c.fill_round_rect(cx - s * 3 / 8, ccy + s / 32, s * 3 / 4, s / 5, s / 12, g);
      break;
    }
    case HomeApp::Music: {
      // Eighth note — oval head, stem, tapered pennant; optically centered.
      const int head_w = s * 3 / 8;
      const int head_h = s * 9 / 32;
      const int hx = cx - s / 10;
      const int hy = y0 + s * 23 / 32;
      c.fill_round_rect(hx - head_w / 2, hy - head_h / 2, head_w, head_h, head_h / 2, g);
      const int stem_w = std::max(3, s / 12);
      const int stem_x = hx + head_w / 2 - stem_w;
      const int stem_top = y0 + s / 6;
      c.fill_rect(stem_x, stem_top, stem_w, hy - stem_top - head_h / 4, g);
      const int flag_h = s * 9 / 32;
      const int flag_w = s * 5 / 16;
      for (int i = 0; i < flag_h; ++i) {
        // Soft taper (not a brick).
        const int fw = flag_w - (flag_w * i * i) / (flag_h * flag_h);
        c.hline(stem_x + stem_w - 1, stem_top + i, std::max(2, fw), g);
      }
      break;
    }
    case HomeApp::Settings: {
      // Eight-tooth gear + hub — kept as the set’s reference silhouette weight.
      const int r_body = s * 5 / 16;
      const int tooth_len = std::max(5, s / 8);
      const int tooth_w = std::max(5, s / 7);
      constexpr int kTeeth = 8;
      for (int k = 0; k < kTeeth; ++k) {
        const double ang = k * 2.0 * kPi / kTeeth;
        const double ca = std::cos(ang), sa = std::sin(ang);
        for (int t = 0; t < tooth_len; ++t) {
          dot(c,
              cx + static_cast<int>(std::lround((r_body - 1 + t) * ca)),
              cy + static_cast<int>(std::lround((r_body - 1 + t) * sa)), tooth_w, g);
        }
      }
      dot(c, cx, cy, r_body * 2, g);
      dot(c, cx, cy, std::max(8, s * 5 / 16), bg);
      break;
    }
    case HomeApp::Update: {
      // Dual circular arrows — heavy arcs matching filled-glyph weight.
      const int r = s * 3 / 8;
      const int thickness = std::max(5, s / 7);
      const double a0 = -kPi / 2.0 + 0.65;
      const double a1 = kPi / 2.0 - 0.15;
      arc(c, cx, cy, r, a0, a1, g, thickness);
      arc(c, cx, cy, r, a0 + kPi, a1 + kPi, g, thickness);
      const int hlen = std::max(8, s / 5);
      arrowhead(c, cx + static_cast<int>(std::lround(r * std::cos(a1))),
                cy + static_cast<int>(std::lround(r * std::sin(a1))), a1 + kPi / 2.0, hlen, g);
      arrowhead(c, cx + static_cast<int>(std::lround(r * std::cos(a1 + kPi))),
                cy + static_cast<int>(std::lround(r * std::sin(a1 + kPi))), a1 + kPi + kPi / 2.0,
                hlen, g);
      break;
    }
    case HomeApp::Reading: {
      // Open book — two filled pages + spine; one bold rule each side.
      const int w = s * 7 / 8;
      const int h = s * 5 / 8;
      const int top = cy - h / 2;
      const int half = w / 2 - stroke;
      c.fill_round_rect(cx - w / 2, top, half, h, s / 8, g);
      c.fill_round_rect(cx + stroke, top, half, h, s / 8, g);
      c.fill_rect(cx - stroke / 2, top, stroke, h, g);
      // One rule per page — enough to say "pages", not a window grid.
      c.fill_rect(cx - w / 2 + half / 4, top + h / 2 - stroke / 2, half / 2, stroke, bg);
      c.fill_rect(cx + stroke + half / 4, top + h / 2 - stroke / 2, half / 2, stroke, bg);
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
