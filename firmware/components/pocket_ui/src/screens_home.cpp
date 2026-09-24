#include "pocket/app.hpp"
#include <algorithm>

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "Update",
    "",      "",       "",      "",     "",        "",      "",         "",
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

/** Calm monochrome glyphs — bold single-weight strokes, minimal fills, e-ink friendly. */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = size;
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  switch (app) {
    case HomeApp::Notes:
      c.stroke_round_rect(x0 + 5, y0 + 3, s - 10, s - 6, 4, g, 2);
      thick_hline(c, x0 + 11, y0 + s / 3, s - 22, g);
      thick_hline(c, x0 + 11, y0 + s / 2, s - 22, g);
      thick_hline(c, x0 + 11, y0 + (2 * s) / 3, s - 26, g);
      break;
    case HomeApp::Ledger:
      c.fill_rect(x0 + 7, y0 + s / 4 - 1, s - 14, 3, g);
      c.fill_rect(x0 + 7, y0 + s / 2 - 1, s - 14, 3, g);
      c.fill_rect(x0 + 7, y0 + (3 * s) / 4 - 1, s - 14, 3, g);
      c.fill_round_rect(x0 + 3, y0 + s / 4 - 4, 5, 5, 2, g);
      c.fill_round_rect(x0 + 3, y0 + s / 2 - 4, 5, 5, 2, g);
      c.fill_round_rect(x0 + 3, y0 + (3 * s) / 4 - 4, 5, 5, 2, g);
      break;
    case HomeApp::Clock:
      c.stroke_round_rect(cx - s / 2 + 3, cy - s / 2 + 3, s - 6, s - 6, (s - 6) / 2, g, 2);
      thick_vline(c, cx, cy - s / 4, s / 4, g);
      thick_hline(c, cx, cy, s / 5, g);
      break;
    case HomeApp::Pass:
      c.stroke_round_rect(x0 + 3, y0 + 9, s - 6, s - 18, 5, g, 2);
      thick_hline(c, x0 + 11, cy - 3, s - 22, g);
      c.fill_round_rect(x0 + 11, cy + 6, 8, 8, 4, g);
      break;
    case HomeApp::Weather:
      c.fill_round_rect(cx - 3, y0 + 4, 6, 6, 3, g);
      c.stroke_round_rect(cx - 11, cy - 6, 22, 15, 7, g, 2);
      break;
    case HomeApp::Music:
      thick_vline(c, cx + 4, y0 + 6, s - 14, g);
      c.line(cx + 4, y0 + 6, cx + 4 + s / 3, y0 + 3, g);
      c.line(cx + 5, y0 + 6, cx + 5 + s / 3, y0 + 3, g);
      c.fill_round_rect(cx - 9, cy + 6, 11, 9, 4, g);
      break;
    case HomeApp::Settings:
      c.stroke_round_rect(cx - s / 3, cy - s / 3, (2 * s) / 3, (2 * s) / 3, 6, g, 2);
      c.fill_round_rect(cx - 4, cy - 4, 8, 8, 4, g);
      break;
    case HomeApp::Update:
      c.stroke_round_rect(cx - s / 2 + 4, cy - s / 2 + 4, s - 8, s - 8, (s - 8) / 2, g, 2);
      thick_vline(c, cx, cy - s / 4 + 2, s / 2 - 6, g);
      c.line(cx - 6, cy - 2, cx, cy - s / 4 + 2, g);
      c.line(cx + 6, cy - 2, cx, cy - s / 4 + 2, g);
      break;
    default:
      break;
  }
}

HomeApp slot_app(int i) { return static_cast<HomeApp>(i); }

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
