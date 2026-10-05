#include "pocket/app.hpp"
#include <algorithm>
#include <cmath>

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "Update",
    "Reading", "",     "",      "",     "",        "",      "",         "",
};

/** Bold geometric strokes (2px) — reads crisply on e-ink at small tile sizes. */
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
  c.fill_round_rect(cx - d / 2, cy - d / 2, d, d, d / 2, g);
}
/** Outline color `g` implies the surface it sits on: a white (G3) stroke means
 * we're drawing over a filled-black focus tile, so the hole must clear to G0. */
Gray surface_for(Gray g) { return g == Gray::G3 ? Gray::G0 : Gray::G3; }
void ring(Canvas& c, int cx, int cy, int d, Gray g, int thickness = 2) {
  c.stroke_round_rect(cx - d / 2, cy - d / 2, d, d, d / 2, g, thickness, surface_for(g));
}
void thick_line(Canvas& c, int x0, int y0, int x1, int y1, Gray g) {
  c.line(x0, y0, x1, y1, g);
  c.line(x0 + 1, y0, x1 + 1, y1, g);
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
 * Professional, calm monochrome glyphs — bold single-weight strokes, deliberate
 * fills, no decorative clutter. Each icon reads clearly at 32px on e-ink.
 */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = size;
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  switch (app) {
    case HomeApp::Notes:
      // Lined notepad — rounded card with three bold rule lines.
      c.stroke_round_rect(x0 + 5, y0 + 2, s - 11, s - 4, 3, g, 2, surface_for(g));
      thick_hline(c, x0 + 10, y0 + s / 3 + 1, s - 20, g);
      thick_hline(c, x0 + 10, y0 + s / 2 + 2, s - 20, g);
      thick_hline(c, x0 + 10, y0 + (2 * s) / 3 + 3, s - 24, g);
      break;
    case HomeApp::Ledger:
      // Columnar balance sheet: a ruled card with a right-hand numbers column.
      c.stroke_round_rect(x0 + 5, y0 + 2, s - 10, s - 4, 3, g, 2, surface_for(g));
      thick_vline(c, x0 + s - 13, y0 + 5, s - 10, g);
      c.hline(x0 + 9, y0 + 9, s - 24, g);
      c.hline(x0 + 9, y0 + 15, s - 24, g);
      c.hline(x0 + 9, y0 + 21, s - 24, g);
      dot(c, x0 + s - 9, y0 + 10, 3, g);
      dot(c, x0 + s - 9, y0 + 16, 3, g);
      dot(c, x0 + s - 9, y0 + 22, 3, g);
      break;
    case HomeApp::Clock:
      // Clean clock face, hour ticks at 12/3/6/9, hands pointing to ~10:10.
      ring(c, cx, cy, s - 6, g);
      dot(c, cx, y0 + 5, 3, g);
      dot(c, cx, y0 + s - 5, 3, g);
      dot(c, x0 + 5, cy, 3, g);
      dot(c, x0 + s - 5, cy, 3, g);
      thick_line(c, cx, cy, cx - s / 5, cy - s / 6, g);
      thick_line(c, cx, cy, cx + s / 5, cy - s / 10, g);
      dot(c, cx, cy, 4, g);
      break;
    case HomeApp::Pass:
      // Boarding-pass stub: wide rounded card, a divider rule, barcode ticks below.
      c.stroke_round_rect(x0 + 1, y0 + 6, s - 2, s - 13, 6, g, 2, surface_for(g));
      thick_hline(c, x0 + 4, cy - 1, s - 8, g);
      c.vline(cx - 9, cy + 4, 6, g);
      c.vline(cx - 5, cy + 4, 6, g);
      c.vline(cx - 1, cy + 4, 6, g);
      c.vline(cx + 3, cy + 4, 6, g);
      c.vline(cx + 7, cy + 4, 6, g);
      break;
    case HomeApp::Weather: {
      // Flat, minimal pairing that reads clearly at 32px: a solid sun disc peeking
      // from behind a wide puffy cloud (three bumps + a base band), no fussy rays.
      dot(c, x0 + s - 11, y0 + 7, 11, g);
      const int ccy = y0 + s - 11;
      dot(c, x0 + 7, ccy, 9, g);
      dot(c, x0 + 14, ccy - 4, 13, g);
      dot(c, x0 + 21, ccy, 9, g);
      c.fill_round_rect(x0 + 2, ccy, s - 6, 8, 4, g);
      break;
    }
    case HomeApp::Music:
      // Two eighth-notes: filled note-heads, straight stems, beamed flag.
      dot(c, x0 + 7, y0 + s - 9, 7, g);
      thick_vline(c, x0 + 10, y0 + 6, s - 15, g);
      dot(c, x0 + s / 2 + 3, y0 + s - 11, 7, g);
      thick_vline(c, x0 + s / 2 + 6, y0 + 3, s - 14, g);
      thick_hline(c, x0 + 10, y0 + 6, s / 2 - 4, g);
      thick_hline(c, x0 + 10, y0 + 9, s / 2 - 7, g);
      break;
    case HomeApp::Settings:
      // 8-tooth gear ring around a hollow hub — unmistakably "settings".
      {
        const int r_out = s / 2 - 1;
        const int r_tooth = s / 10;
        constexpr int kTeeth = 8;
        for (int k = 0; k < kTeeth; ++k) {
          const double ang = k * 2.0 * 3.14159265 / kTeeth;
          const int tx = cx + static_cast<int>(r_out * std::cos(ang));
          const int ty = cy + static_cast<int>(r_out * std::sin(ang));
          dot(c, tx, ty, 2 * r_tooth, g);
        }
        dot(c, cx, cy, s - 10, g);
        dot(c, cx, cy, s - 20, surface_for(g));
      }
      break;
    case HomeApp::Update:
      // Circular-arrow refresh glyph: a 3/4 ring (clockwise) with a solid arrowhead
      // at the open end, pointing in the direction of travel.
      {
        const int r = s / 2 - 4;
        const double a0 = -kPi / 2.0 - 0.35;  // just before north, going clockwise
        const double a1 = kPi * 0.95;          // sweeps through east/south to near west
        arc(c, cx, cy, r, a0, a1, g, 3);
        const int hx = cx + static_cast<int>(std::lround(r * std::cos(a0)));
        const int hy = cy + static_cast<int>(std::lround(r * std::sin(a0)));
        thick_line(c, hx - 7, hy + 1, hx + 1, hy - 4, g);
        thick_line(c, hx - 1, hy + 7, hx + 1, hy - 4, g);
      }
      break;
    case HomeApp::Reading:
      // Open book — two pages on a bold spine, each with a short text rule.
      c.stroke_round_rect(x0 + 3, y0 + 5, s / 2 - 2, s - 10, 3, g, 2, surface_for(g));
      c.stroke_round_rect(cx + 1, y0 + 5, s / 2 - 2, s - 10, 3, g, 2, surface_for(g));
      thick_vline(c, cx, y0 + 5, s - 10, g);
      c.hline(x0 + 8, cy, s / 2 - 11, g);
      c.hline(cx + 6, cy, s / 2 - 11, g);
      break;
    default:
      break;
  }
}

/** Display order: real content apps, then Reading, then the always-on Settings/Update tiles —
 * keeps the utility tiles last regardless of HomeApp's underlying (bitmask-compatible) index. */
constexpr int kHomeOrder[kHomeGridSlots] = {0, 1, 2, 3, 4, 5, 8, 6, 7, 9, 10, 11, 12, 13, 14, 15};

HomeApp slot_app(int i) { return static_cast<HomeApp>(kHomeOrder[i]); }

void home_grid_metrics(int n_focus, int* out_grid_top, int* out_tile_w, int* out_tile_h, int* out_gap_x,
                       int* out_gap_y) {
  constexpr int kCols = 2;
  constexpr int kGapX = 20;
  constexpr int kGapY = 16;
  constexpr int kBrandBand = 56;
  constexpr int kGridTopMin = kContentTop + kBrandBand;
  constexpr int kBottomPad = 28;
  constexpr int kTileH = 108;
  const int rows = std::max(1, (n_focus + kCols - 1) / kCols);
  const int tile_w = (kCanvasW - 2 * kSideMargin - kGapX) / kCols;
  const int grid_h = rows * kTileH + (rows - 1) * kGapY;
  const int avail = kCanvasH - kGridTopMin - kBottomPad;
  const int grid_top = kGridTopMin + std::max(0, (avail - grid_h) / 8);
  *out_grid_top = grid_top;
  *out_tile_w = tile_w;
  *out_tile_h = kTileH;
  *out_gap_x = kGapX;
  *out_gap_y = kGapY;
}

}  // namespace

void App::render_home() {
  draw_status_bar();

  // Product brand — no version number on Home.
  canvas_.draw_text(kSideMargin, kContentTop, kProductName, Canvas::TextRole::WordMark, Gray::G0);
  const int brand_rule_y = kContentTop + canvas_.text_height(Canvas::TextRole::WordMark) + 6;
  canvas_.hline(kSideMargin, brand_rule_y, 96, Gray::G1);

  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n_focus++] = a;
  }
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  int grid_top = 0, tile_w = 0, tile_h = 0, gap_x = 0, gap_y = 0;
  home_grid_metrics(n_focus, &grid_top, &tile_w, &tile_h, &gap_x, &gap_y);
  constexpr int kCols = 2;
  constexpr int kRadius = 18;
  const int glyph = 32;
  const int label_h = canvas_.text_height(Canvas::TextRole::Secondary);

  for (int i = 0; i < n_focus; ++i) {
    const int col = i % kCols;
    const int row = i / kCols;
    const int x = kSideMargin + col * (tile_w + gap_x);
    const int y = grid_top + row * (tile_h + gap_y);
    const HomeApp a = focusable[i];
    const bool is_focus = a == focused;
    const int label_i = static_cast<int>(a);
    const char* label =
        (label_i >= 0 && label_i < kHomeGridSlots) ? kAppLabels[label_i] : "";
    const int glyph_cy = y + 18 + glyph / 2;
    const int label_y = y + tile_h - label_h - 14;

    if (is_focus) {
      canvas_.fill_round_rect(x, y, tile_w, tile_h, kRadius, Gray::G0);
      draw_app_glyph(canvas_, a, x + tile_w / 2, glyph_cy, glyph, Gray::G3);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        canvas_.draw_text(x + (tile_w - tw) / 2, label_y, label, Canvas::TextRole::Secondary, Gray::G3);
      }
    } else {
      canvas_.stroke_round_rect(x, y, tile_w, tile_h, kRadius, Gray::G1, 2);
      draw_app_glyph(canvas_, a, x + tile_w / 2, glyph_cy, glyph, Gray::G0);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        canvas_.draw_text(x + (tile_w - tw) / 2, label_y, label, Canvas::TextRole::Secondary, Gray::G0);
      }
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
