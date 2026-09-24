#include "pocket/app.hpp"
#include <algorithm>

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "Update",
    "",      "",       "",      "",     "",        "",      "",         "",
};

/** Draw a thick ring (e-ink friendly) centered at cx,cy. */
void stroke_ring(Canvas& c, int cx, int cy, int r, Gray g) {
  c.stroke_rect(cx - r, cy - r, 2 * r, 2 * r, g);
  c.stroke_rect(cx - r + 1, cy - r + 1, 2 * r - 2, 2 * r - 2, g);
}

/** Abstract monochrome glyph — open, light strokes for a calmer Home. */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = size;
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  switch (app) {
    case HomeApp::Notes: {
      c.stroke_rect(x0 + 6, y0 + 4, s - 14, s - 8, g);
      c.hline(x0 + 12, y0 + s / 3, s - 24, g);
      c.hline(x0 + 12, y0 + s / 2, s - 24, g);
      c.hline(x0 + 12, y0 + (2 * s) / 3, s - 28, g);
      break;
    }
    case HomeApp::Ledger: {
      const int bar_h = std::max(3, s / 9);
      const int gap = std::max(5, (s - 3 * bar_h) / 4);
      for (int i = 0; i < 3; ++i) {
        const int by = y0 + gap + i * (bar_h + gap);
        c.fill_rect(x0 + 8, by, s - 16, bar_h, g);
      }
      break;
    }
    case HomeApp::Clock: {
      const int r = s / 2 - 4;
      stroke_ring(c, cx, cy, r, g);
      c.vline(cx, cy - r + 8, r - 6, g);
      c.hline(cx, cy, r / 2, g);
      break;
    }
    case HomeApp::Pass: {
      c.stroke_rect(x0 + 5, y0 + 10, s - 10, s - 20, g);
      c.hline(x0 + 12, y0 + 18, s - 24, g);
      for (int i = 0; i < 3; ++i) {
        c.vline(x0 + 14 + i * 8, cy + 2, s / 5, g);
      }
      break;
    }
    case HomeApp::Weather: {
      const int r = std::max(4, s / 6);
      c.stroke_rect(cx - r, cy - r, 2 * r, 2 * r, g);
      c.vline(cx, y0 + 6, 5, g);
      c.vline(cx, y0 + s - 11, 5, g);
      c.hline(x0 + 6, cy, 5, g);
      c.hline(x0 + s - 11, cy, 5, g);
      break;
    }
    case HomeApp::Music: {
      c.fill_rect(cx - 8, cy + 6, 12, 8, g);
      c.vline(cx + 3, y0 + 8, s - 16, g);
      c.hline(cx + 3, y0 + 8, 8, g);
      break;
    }
    case HomeApp::Settings: {
      const int r = s / 2 - 5;
      stroke_ring(c, cx, cy, r, g);
      c.fill_rect(cx - 3, cy - 3, 6, 6, g);
      break;
    }
    case HomeApp::Update: {
      const int r = s / 2 - 5;
      stroke_ring(c, cx, cy, r, g);
      c.hline(cx + 2, y0 + 8, 8, g);
      c.vline(cx + 8, y0 + 5, 8, g);
      break;
    }
    default:
      break;
  }
}

HomeApp slot_app(int i) { return static_cast<HomeApp>(i); }

/** Tile geometry for focus-region partials (matches render_home packing). */
bool home_tile_rect(const DeviceConfig& cfg, int focus_index, int* out_x, int* out_y, int* out_w,
                    int* out_h) {
  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg, a)) focusable[n_focus++] = a;
  }
  if (n_focus <= 0 || focus_index < 0 || focus_index >= n_focus) return false;
  constexpr int kCols = 2;
  constexpr int kGapX = 18;
  constexpr int kGapY = 14;
  constexpr int kBrandBand = 56;
  constexpr int kGridTop = kContentTop + kBrandBand;
  constexpr int kBottomPad = 24;
  const int rows = std::max(1, (n_focus + kCols - 1) / kCols);
  const int tile_w = (kCanvasW - 2 * kSideMargin - kGapX) / kCols;
  const int tile_h = (kCanvasH - kGridTop - kBottomPad - (rows - 1) * kGapY) / rows;
  const int col = focus_index % kCols;
  const int row = focus_index / kCols;
  *out_x = kSideMargin + col * (tile_w + kGapX);
  *out_y = kGridTop + row * (tile_h + kGapY);
  *out_w = tile_w;
  *out_h = tile_h;
  return true;
}

}  // namespace

void App::render_home() {
  draw_status_bar();

  // Brand chrome: product name only — never “Version 1” on Home.
  canvas_.draw_text(kSideMargin, kContentTop, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
  const int brand_rule_y = kContentTop + canvas_.text_height(Canvas::TextRole::WordMark) + 8;
  canvas_.hline(kSideMargin, brand_rule_y, kContentW, Gray::G2);

  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n_focus++] = a;
  }
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  constexpr int kCols = 2;
  constexpr int kGapX = 18;
  constexpr int kGapY = 14;
  constexpr int kBrandBand = 56;
  constexpr int kGridTop = kContentTop + kBrandBand;
  constexpr int kBottomPad = 24;
  constexpr int kRadius = 16;
  const int rows = std::max(1, (n_focus + kCols - 1) / kCols);
  const int tile_w = (kCanvasW - 2 * kSideMargin - kGapX) / kCols;
  const int tile_h = (kCanvasH - kGridTop - kBottomPad - (rows - 1) * kGapY) / rows;
  const int glyph = std::min(48, std::max(28, tile_h - 36));
  const int label_h = canvas_.text_height(Canvas::TextRole::Secondary);

  for (int i = 0; i < n_focus; ++i) {
    const int col = i % kCols;
    const int row = i / kCols;
    const int x = kSideMargin + col * (tile_w + kGapX);
    const int y = kGridTop + row * (tile_h + kGapY);
    const HomeApp a = focusable[i];
    const bool is_focus = a == focused;
    const int label_i = static_cast<int>(a);
    const char* label =
        (label_i >= 0 && label_i < kHomeGridSlots) ? kAppLabels[label_i] : "";

    const int glyph_cy = y + (tile_h - label_h - 10) / 2;
    const int label_y = y + tile_h - label_h - 12;

    if (is_focus) {
      canvas_.fill_round_rect(x, y, tile_w, tile_h, kRadius, Gray::G0);
      draw_app_glyph(canvas_, a, x + tile_w / 2, glyph_cy, glyph, Gray::G3);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        if (tw <= tile_w - 16) {
          canvas_.draw_text(x + (tile_w - tw) / 2, label_y, label, Canvas::TextRole::Secondary, Gray::G3);
        } else {
          canvas_.draw_text_fit(x + 8, label_y, tile_w - 16, label, Canvas::TextRole::Secondary, Gray::G3);
        }
      }
    } else {
      // Soft outline — 2px rounded border, not a hard rectangle.
      canvas_.stroke_round_rect(x, y, tile_w, tile_h, kRadius, Gray::G1, 2);
      draw_app_glyph(canvas_, a, x + tile_w / 2, glyph_cy, glyph, Gray::G0);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        if (tw <= tile_w - 16) {
          canvas_.draw_text(x + (tile_w - tw) / 2, label_y, label, Canvas::TextRole::Secondary, Gray::G0);
        } else {
          canvas_.draw_text_fit(x + 8, label_y, tile_w - 16, label, Canvas::TextRole::Secondary, Gray::G0);
        }
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
    const int prev = focus_.index;
    focus_.move(e == InputEvent::Up ? -1 : 1);
    // Dirty only the two tiles that changed — faster than full content band.
    int x = 0, y = 0, w = 0, h = 0;
    if (home_tile_rect(cfg_, prev, &x, &y, &w, &h)) mark_region_dirty(x, y, w, h);
    if (home_tile_rect(cfg_, focus_.index, &x, &y, &w, &h)) mark_region_dirty(x, y, w, h);
  } else if (e == InputEvent::Select && n > 0) {
    const HomeApp launch = focusable[focus_.index];
    // Parental: require PIN before opening gated apps (unlock PIN stays on-device).
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
