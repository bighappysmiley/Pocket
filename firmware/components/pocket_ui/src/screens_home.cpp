#include "pocket/app.hpp"

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "Update",
    "",      "",       "",      "",     "",        "",      "",         "",
};

/** Abstract monochrome glyph — thicker strokes for e-ink clarity. */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = size;
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  switch (app) {
    case HomeApp::Notes:
      c.stroke_rect(x0 + 3, y0 + 2, s - 6, s - 4, g);
      c.stroke_rect(x0 + 4, y0 + 3, s - 8, s - 6, g);
      c.hline(x0 + 8, y0 + s / 3, s - 16, g);
      c.hline(x0 + 8, y0 + s / 2, s - 16, g);
      c.hline(x0 + 8, y0 + (2 * s) / 3, s - 18, g);
      break;
    case HomeApp::Ledger:
      c.stroke_rect(x0 + 2, y0 + 5, s - 4, s - 10, g);
      c.vline(cx, y0 + 9, s - 18, g);
      c.vline(cx + 1, y0 + 9, s - 18, g);
      c.hline(x0 + 6, cy, s - 12, g);
      c.hline(x0 + 6, cy + 1, s - 12, g);
      break;
    case HomeApp::Clock:
      c.stroke_rect(x0 + 2, y0 + 2, s - 4, s - 4, g);
      c.stroke_rect(x0 + 3, y0 + 3, s - 6, s - 6, g);
      c.vline(cx, y0 + 8, s / 3, g);
      c.vline(cx + 1, y0 + 8, s / 3, g);
      c.hline(cx, cy, s / 3, g);
      c.hline(cx, cy + 1, s / 3, g);
      break;
    case HomeApp::Pass:
      c.stroke_rect(x0 + 5, y0 + 4, s - 10, s - 8, g);
      c.fill_rect(x0 + 9, y0 + 9, s - 18, s - 18, g);
      break;
    case HomeApp::Weather:
      c.fill_rect(cx - 5, y0 + 4, 10, 10, g);
      c.stroke_rect(x0 + 6, y0 + 12, s - 12, s - 18, g);
      c.hline(x0 + 10, y0 + s - 10, s - 20, g);
      break;
    case HomeApp::Music: {
      c.vline(cx + 7, y0 + 5, s - 12, g);
      c.vline(cx + 8, y0 + 5, s - 12, g);
      c.fill_rect(cx - 9, cy + 3, 16, 12, g);
      c.hline(cx - 2, y0 + 8, 10, g);
      break;
    }
    case HomeApp::Settings:
      c.stroke_rect(x0 + 5, y0 + 5, s - 10, s - 10, g);
      c.stroke_rect(x0 + 6, y0 + 6, s - 12, s - 12, g);
      c.fill_rect(cx - 4, cy - 4, 8, 8, g);
      break;
    case HomeApp::Update: {
      // Circular arrow — update
      c.stroke_rect(x0 + 4, y0 + 4, s - 8, s - 8, g);
      c.vline(cx, y0 + 8, s / 2, g);
      c.hline(cx - 6, cy + 4, 12, g);
      c.hline(cx + 2, y0 + 10, 8, g);
      c.vline(cx + 8, y0 + 10, 8, g);
      break;
    }
    default:
      break;
  }
}

HomeApp slot_app(int i) { return static_cast<HomeApp>(i); }

}  // namespace

void App::render_home() {
  draw_status_bar();
  canvas_.draw_text(kSideMargin, kContentTop, "Pocket", Canvas::TextRole::WordMark, Gray::G0);

  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n_focus++] = a;
  }
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  // Pack visible apps into a dense 2-column grid (larger tiles than a fixed 2×8).
  constexpr int kCols = 2;
  constexpr int kGapX = 16;
  constexpr int kGapY = 10;
  constexpr int kGridTop = kContentTop + 44;
  constexpr int kBottomPad = 16;
  const int rows = std::max(1, (n_focus + kCols - 1) / kCols);
  const int tile_w = (kCanvasW - 2 * kSideMargin - kGapX) / kCols;
  const int tile_h = (kCanvasH - kGridTop - kBottomPad - (rows - 1) * kGapY) / rows;
  const int glyph = std::min(52, std::max(28, tile_h - 28));

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
      draw_app_glyph(canvas_, a, x + tile_w / 2, y + (tile_h - 22) / 2, glyph, Gray::G3);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        if (tw <= tile_w - 12) {
          canvas_.draw_text(x + (tile_w - tw) / 2, y + tile_h - 22, label, Canvas::TextRole::Secondary, Gray::G3);
        } else {
          canvas_.draw_text_fit(x + 6, y + tile_h - 22, tile_w - 12, label, Canvas::TextRole::Secondary, Gray::G3);
        }
      }
    } else {
      canvas_.stroke_rect(x, y, tile_w, tile_h, Gray::G1);
      draw_app_glyph(canvas_, a, x + tile_w / 2, y + (tile_h - 22) / 2, glyph, Gray::G0);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        if (tw <= tile_w - 12) {
          canvas_.draw_text(x + (tile_w - tw) / 2, y + tile_h - 22, label, Canvas::TextRole::Secondary, Gray::G0);
        } else {
          canvas_.draw_text_fit(x + 6, y + tile_h - 22, tile_w - 12, label, Canvas::TextRole::Secondary, Gray::G0);
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
  if (e == InputEvent::Up) {
    focus_.move(-1);
    mark_content_dirty();
  } else if (e == InputEvent::Down) {
    focus_.move(1);
    mark_content_dirty();
  } else if (e == InputEvent::Select && n > 0) {
    switch (focusable[focus_.index]) {
      case HomeApp::Notes:
        notes_tab_ = 0;
        nav_.push(ScreenId::NotesList);
        break;
      case HomeApp::Ledger:
        nav_.push(ScreenId::LedgerComingSoon);
        break;
      case HomeApp::Clock:
        clock_tab_ = 0;
        nav_.push(ScreenId::ClockFace);
        break;
      case HomeApp::Pass:
        nav_.push(ScreenId::PassList);
        break;
      case HomeApp::Weather:
        nav_.push(ScreenId::WeatherMain);
        break;
      case HomeApp::Music:
        music_index_ = 0;
        nav_.push(ScreenId::MusicList);
        break;
      case HomeApp::Settings:
        nav_.push(ScreenId::SettingsRoot);
        break;
      case HomeApp::Update:
        begin_firmware_update();
        return;
      default:
        return;
    }
    after_nav();
  } else if (e == InputEvent::Back) {
    // stay on home
  }
}

}  // namespace pocket
