#include "pocket/app.hpp"

namespace pocket {

static const char* kAppLabels[] = {"Notes", "Ledger", "Clock", "Pass", "Weather", "Settings"};

void App::render_home() {
  draw_status_bar();
  canvas_.draw_text(kSideMargin, kContentTop, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
  draw_home_clock();

  // Visible apps in locked order, collapsed empty slots
  HomeApp order[] = {HomeApp::Notes, HomeApp::Ledger, HomeApp::Clock,
                     HomeApp::Pass,  HomeApp::Weather, HomeApp::Settings};
  HomeApp visible[6];
  int n = 0;
  for (auto a : order) {
    if (home_app_visible(cfg_, a)) visible[n++] = a;
  }
  focus_.count = n;

  const int tile_w = (kCanvasW - 48) / 2;
  const int tile_h = 104;
  // Below wordmark + clock band (content top + 48 + 120).
  constexpr int kGridTop = kContentTop + 200;
  for (int i = 0; i < n; ++i) {
    int col = i % 2;
    int row = i / 2;
    int x = 16 + col * (tile_w + 16);
    int y = kGridTop + row * (tile_h + 16);
    const char* label = kAppLabels[static_cast<int>(visible[i])];
    if (i == focus_.index) {
      canvas_.draw_focus_tile(x, y, tile_w, tile_h, label, Canvas::TextRole::Body);
    } else {
      canvas_.stroke_rect(x, y, tile_w, tile_h, Gray::G2);
      canvas_.draw_text_centered(x + tile_w / 2, y + tile_h / 2 - 9, label, Canvas::TextRole::Body, Gray::G0);
    }
  }
}

void App::handle_home(InputEvent e) {
  HomeApp order[] = {HomeApp::Notes, HomeApp::Ledger, HomeApp::Clock,
                     HomeApp::Pass,  HomeApp::Weather, HomeApp::Settings};
  HomeApp visible[6];
  int n = 0;
  for (auto a : order) {
    if (home_app_visible(cfg_, a)) visible[n++] = a;
  }
  focus_.count = n;
  if (e == InputEvent::Up) {
    focus_.move(-1);
    mark_content_dirty();
  } else if (e == InputEvent::Down) {
    focus_.move(1);
    mark_content_dirty();
  } else if (e == InputEvent::Select && n > 0) {
    switch (visible[focus_.index]) {
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
      case HomeApp::Settings:
        nav_.push(ScreenId::SettingsRoot);
        break;
    }
    after_nav();
  } else if (e == InputEvent::Back) {
    // stay on home
  }
}

}  // namespace pocket
