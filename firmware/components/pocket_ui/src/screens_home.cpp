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

/** Abstract monochrome glyph — thicker strokes for e-ink clarity. */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = size;
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  switch (app) {
    case HomeApp::Notes: {
      // Page with folded corner + three lines
      c.stroke_rect(x0 + 4, y0 + 2, s - 10, s - 4, g);
      c.stroke_rect(x0 + 5, y0 + 3, s - 12, s - 6, g);
      c.hline(x0 + s - 10, y0 + 2, 6, g);
      c.vline(x0 + s - 4, y0 + 2, 6, g);
      c.hline(x0 + s - 10, y0 + 8, 6, g);
      c.hline(x0 + 10, y0 + s / 3, s - 20, g);
      c.hline(x0 + 10, y0 + s / 2, s - 20, g);
      c.hline(x0 + 10, y0 + (2 * s) / 3, s - 22, g);
      break;
    }
    case HomeApp::Ledger: {
      // Three equal ledger bars
      const int bar_h = std::max(4, s / 7);
      const int gap = std::max(4, (s - 3 * bar_h) / 4);
      for (int i = 0; i < 3; ++i) {
        const int by = y0 + gap + i * (bar_h + gap);
        c.fill_rect(x0 + 4, by, s - 8, bar_h, g);
      }
      break;
    }
    case HomeApp::Clock: {
      // Round face + hands
      const int r = s / 2 - 3;
      stroke_ring(c, cx, cy, r, g);
      c.vline(cx, cy - r + 6, r - 4, g);
      c.vline(cx + 1, cy - r + 6, r - 4, g);
      c.hline(cx, cy, r / 2 + 2, g);
      c.hline(cx, cy + 1, r / 2 + 2, g);
      break;
    }
    case HomeApp::Pass: {
      // Ticket / badge with barcode ticks
      c.stroke_rect(x0 + 3, y0 + 8, s - 6, s - 16, g);
      c.stroke_rect(x0 + 4, y0 + 9, s - 8, s - 18, g);
      c.fill_rect(x0 + 10, y0 + 14, s - 20, 8, g);
      for (int i = 0; i < 4; ++i) {
        c.vline(x0 + 12 + i * 6, cy + 2, s / 4, g);
      }
      break;
    }
    case HomeApp::Weather: {
      // Sun disk + rays
      const int r = s / 5;
      c.fill_rect(cx - r, cy - r, 2 * r, 2 * r, g);
      c.vline(cx, y0 + 4, 6, g);
      c.vline(cx, y0 + s - 10, 6, g);
      c.hline(x0 + 4, cy, 6, g);
      c.hline(x0 + s - 10, cy, 6, g);
      c.hline(x0 + 8, y0 + 10, 4, g);
      c.hline(x0 + s - 12, y0 + s - 14, 4, g);
      break;
    }
    case HomeApp::Music: {
      // Note: oval head + stem + flag
      c.fill_rect(cx - 10, cy + 4, 14, 10, g);
      c.vline(cx + 4, y0 + 6, s - 14, g);
      c.vline(cx + 5, y0 + 6, s - 14, g);
      c.hline(cx + 5, y0 + 6, 10, g);
      c.vline(cx + 14, y0 + 6, 10, g);
      break;
    }
    case HomeApp::Settings: {
      // Gear: ring + spokes
      const int r = s / 2 - 4;
      stroke_ring(c, cx, cy, r, g);
      c.fill_rect(cx - 4, cy - 4, 8, 8, g);
      c.vline(cx, y0 + 4, 6, g);
      c.vline(cx, y0 + s - 10, 6, g);
      c.hline(x0 + 4, cy, 6, g);
      c.hline(x0 + s - 10, cy, 6, g);
      break;
    }
    case HomeApp::Update: {
      // Circular arrow: arc + chevron
      const int r = s / 2 - 4;
      stroke_ring(c, cx, cy, r, g);
      // Open the ring at top-right and draw arrowhead
      c.fill_rect(cx + 2, y0 + 4, r - 2, 8, Gray::G3);
      c.hline(cx + 2, y0 + 8, 10, g);
      c.vline(cx + 10, y0 + 4, 10, g);
      c.hline(cx + 6, y0 + 4, 6, g);
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
  constexpr int kGapX = 16;
  constexpr int kGapY = 12;
  constexpr int kGridTop = kContentTop + 48;
  constexpr int kBottomPad = 20;
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
  canvas_.draw_text(kSideMargin, kContentTop, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
  canvas_.draw_text(kSideMargin + canvas_.text_width("Pocket", Canvas::TextRole::WordMark) + 10,
                    kContentTop + 6, "Version 1", Canvas::TextRole::Secondary, Gray::G1);

  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n_focus++] = a;
  }
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  // Pack visible apps into a dense 2-column grid with clearer gaps.
  constexpr int kCols = 2;
  constexpr int kGapX = 16;
  constexpr int kGapY = 12;
  constexpr int kGridTop = kContentTop + 48;
  constexpr int kBottomPad = 20;
  const int rows = std::max(1, (n_focus + kCols - 1) / kCols);
  const int tile_w = (kCanvasW - 2 * kSideMargin - kGapX) / kCols;
  const int tile_h = (kCanvasH - kGridTop - kBottomPad - (rows - 1) * kGapY) / rows;
  const int glyph = std::min(56, std::max(30, tile_h - 30));

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

    if (is_focus) {
      canvas_.fill_rect(x, y, tile_w, tile_h, Gray::G0);
      draw_app_glyph(canvas_, a, x + tile_w / 2, y + (tile_h - 24) / 2, glyph, Gray::G3);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        if (tw <= tile_w - 12) {
          canvas_.draw_text(x + (tile_w - tw) / 2, y + tile_h - 24, label, Canvas::TextRole::Secondary, Gray::G3);
        } else {
          canvas_.draw_text_fit(x + 6, y + tile_h - 24, tile_w - 12, label, Canvas::TextRole::Secondary, Gray::G3);
        }
      }
    } else {
      canvas_.stroke_rect(x, y, tile_w, tile_h, Gray::G1);
      draw_app_glyph(canvas_, a, x + tile_w / 2, y + (tile_h - 24) / 2, glyph, Gray::G0);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        if (tw <= tile_w - 12) {
          canvas_.draw_text(x + (tile_w - tw) / 2, y + tile_h - 24, label, Canvas::TextRole::Secondary, Gray::G0);
        } else {
          canvas_.draw_text_fit(x + 6, y + tile_h - 24, tile_w - 12, label, Canvas::TextRole::Secondary, Gray::G0);
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
