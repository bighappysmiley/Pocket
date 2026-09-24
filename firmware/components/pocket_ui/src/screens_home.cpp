#include "pocket/app.hpp"

namespace pocket {
namespace {

static const char* kAppLabels[kHomeGridSlots] = {
    "Notes", "Ledger", "Clock", "Pass", "Weather", "Music", "Settings", "", "", "", "", "", "", "", "", "",
};

/** Simple monochrome glyph inside a cell (abstract, high-contrast). */
void draw_app_glyph(Canvas& c, HomeApp app, int cx, int cy, int size, Gray g) {
  const int s = size;
  const int x0 = cx - s / 2;
  const int y0 = cy - s / 2;
  switch (app) {
    case HomeApp::Notes:
      c.stroke_rect(x0 + 4, y0 + 2, s - 8, s - 4, g);
      c.hline(x0 + 8, y0 + s / 3, s - 16, g);
      c.hline(x0 + 8, y0 + s / 2, s - 16, g);
      c.hline(x0 + 8, y0 + (2 * s) / 3, s - 20, g);
      break;
    case HomeApp::Ledger:
      c.stroke_rect(x0 + 2, y0 + 6, s - 4, s - 12, g);
      c.vline(cx, y0 + 10, s - 20, g);
      c.hline(x0 + 6, cy, s - 12, g);
      break;
    case HomeApp::Clock:
      c.stroke_rect(x0 + 2, y0 + 2, s - 4, s - 4, g);
      c.vline(cx, y0 + 8, s / 3, g);
      c.hline(cx, cy, s / 4, g);
      break;
    case HomeApp::Pass:
      c.stroke_rect(x0 + 6, y0 + 4, s - 12, s - 8, g);
      c.fill_rect(x0 + 10, y0 + 10, s - 20, s - 20, g);
      break;
    case HomeApp::Weather:
      c.stroke_rect(x0 + 8, y0 + 10, s - 16, s - 20, g);
      c.fill_rect(cx - 4, y0 + 4, 8, 8, g);
      break;
    case HomeApp::Music: {
      // Note stem + head
      c.vline(cx + 6, y0 + 6, s - 14, g);
      c.fill_rect(cx - 8, cy + 4, 14, 10, g);
      break;
    }
    case HomeApp::Settings:
      c.stroke_rect(x0 + 6, y0 + 6, s - 12, s - 12, g);
      c.stroke_rect(cx - 4, cy - 4, 8, 8, g);
      break;
    default:
      break;
  }
}

HomeApp slot_app(int i) { return static_cast<HomeApp>(i); }

}  // namespace

void App::render_home() {
  draw_status_bar();
  // Word mark only — time lives in the status bar (no big clock on Home).
  canvas_.draw_text(kSideMargin, kContentTop, "Pocket", Canvas::TextRole::WordMark, Gray::G0);

  constexpr int kCols = 2;
  constexpr int kRows = 8;
  constexpr int kGapX = 12;
  constexpr int kGapY = 4;
  constexpr int kGridTop = kContentTop + 48;
  const int tile_w = (kCanvasW - 2 * kSideMargin - kGapX) / kCols;
  const int tile_h = (kCanvasH - kGridTop - 8 - (kRows - 1) * kGapY) / kRows;

  // Focus walks only visible real apps in slot order.
  HomeApp focusable[kHomeGridSlots];
  int n_focus = 0;
  for (int i = 0; i < kHomeGridSlots; ++i) {
    const HomeApp a = slot_app(i);
    if (home_app_visible(cfg_, a)) focusable[n_focus++] = a;
  }
  focus_.count = std::max(1, n_focus);
  if (focus_.index >= focus_.count) focus_.index = 0;
  const HomeApp focused = n_focus > 0 ? focusable[focus_.index] : HomeApp::Settings;

  for (int i = 0; i < kHomeGridSlots; ++i) {
    const int col = i % kCols;
    const int row = i / kCols;
    const int x = kSideMargin + col * (tile_w + kGapX);
    const int y = kGridTop + row * (tile_h + kGapY);
    const HomeApp a = slot_app(i);
    const bool real = home_app_is_real(a) && home_app_visible(cfg_, a);
    const bool is_focus = real && a == focused;
    const char* label = kAppLabels[i];

    if (is_focus) {
      canvas_.fill_rect(x, y, tile_w, tile_h, Gray::G0);
      draw_app_glyph(canvas_, a, x + tile_w / 2, y + tile_h / 2 - 10, 36, Gray::G3);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        canvas_.draw_text(x + (tile_w - tw) / 2, y + tile_h - 22, label, Canvas::TextRole::Secondary, Gray::G3);
      }
    } else if (real) {
      canvas_.stroke_rect(x, y, tile_w, tile_h, Gray::G2);
      draw_app_glyph(canvas_, a, x + tile_w / 2, y + tile_h / 2 - 10, 36, Gray::G0);
      if (label && *label) {
        const int tw = canvas_.text_width(label, Canvas::TextRole::Secondary);
        canvas_.draw_text(x + (tile_w - tw) / 2, y + tile_h - 22, label, Canvas::TextRole::Secondary, Gray::G0);
      }
    } else {
      // Empty reserved slot — hairline only, not focusable.
      canvas_.stroke_rect(x + 2, y + 2, tile_w - 4, tile_h - 4, Gray::G2);
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
      default:
        return;
    }
    after_nav();
  } else if (e == InputEvent::Back) {
    // stay on home
  }
}

}  // namespace pocket
