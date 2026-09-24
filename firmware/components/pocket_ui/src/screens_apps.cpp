#include "pocket/app.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace pocket {

void App::render_notes() {
  draw_status_bar();
  const ScreenId s = nav_.current();
  // Tabs
  if (s == ScreenId::NotesList || s == ScreenId::ListsList) {
    if (notes_tab_ == 0) {
      canvas_.draw_focus_tile(16, 36, 100, 36, "Notes", Canvas::TextRole::Secondary);
      canvas_.draw_text(130, 44, "Lists", Canvas::TextRole::Secondary, Gray::G1);
    } else {
      canvas_.draw_text(24, 44, "Notes", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_focus_tile(120, 36, 100, 36, "Lists", Canvas::TextRole::Secondary);
    }
  }

  if (s == ScreenId::NotesList) {
    if (data_.notes.empty()) {
      canvas_.draw_text_centered(kCanvasW / 2, 300, "No notes yet", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text_centered(kCanvasW / 2, 340, "Hold BOOT to dictate a note", Canvas::TextRole::Secondary,
                                 Gray::G1);
    } else {
      focus_.count = static_cast<int>(data_.notes.size()) + 1;
      for (int i = 0; i < static_cast<int>(data_.notes.size()); ++i) {
        int y = 90 + i * 52;
        if (i == focus_.index)
          canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, data_.notes[i].title, Canvas::TextRole::Body);
        else
          canvas_.draw_text(24, y + 14, data_.notes[i].title, Canvas::TextRole::Body, Gray::G0);
      }
    }
    int y = 720;
    bool fab = focus_.index == static_cast<int>(data_.notes.size()) || data_.notes.empty();
    if (data_.notes.empty()) focus_.count = 1;
    if (fab || focus_.index == static_cast<int>(data_.notes.size()))
      canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, "New note", Canvas::TextRole::Body);
    else
      canvas_.draw_text(24, y + 14, "New note", Canvas::TextRole::Body, Gray::G0);
  } else if (s == ScreenId::NotesDetail) {
    if (note_index_ >= 0 && note_index_ < static_cast<int>(data_.notes.size())) {
      auto& n = data_.notes[note_index_];
      canvas_.draw_text(16, 40, n.title, Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(16, 90, n.body, Canvas::TextRole::Body, Gray::G0);
    }
    focus_.count = 2;
    const char* acts[] = {"Dictate", "Delete"};
    for (int i = 0; i < 2; ++i) {
      int y = 680 + i * 48;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 44, acts[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 12, acts[i], Canvas::TextRole::Body, Gray::G0);
    }
  } else if (s == ScreenId::ListsList) {
    if (data_.lists.empty()) {
      canvas_.draw_text_centered(kCanvasW / 2, 300, "No lists yet", Canvas::TextRole::Body, Gray::G0);
    }
    focus_.count = static_cast<int>(data_.lists.size()) + 1;
    for (int i = 0; i < static_cast<int>(data_.lists.size()); ++i) {
      int y = 90 + i * 52;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, data_.lists[i].title, Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, data_.lists[i].title, Canvas::TextRole::Body, Gray::G0);
    }
    canvas_.draw_text(24, 720, "New list", Canvas::TextRole::Body, Gray::G0);
  } else if (s == ScreenId::ListsDetail) {
    if (note_index_ < static_cast<int>(data_.lists.size())) {
      auto& L = data_.lists[note_index_];
      canvas_.draw_text(16, 40, L.title, Canvas::TextRole::ScreenTitle, Gray::G0);
      for (size_t i = 0; i < L.items.size(); ++i) {
        int y = 90 + static_cast<int>(i) * 40;
        std::string row = (L.items[i].checked ? "[x] " : "[ ] ") + L.items[i].text;
        Gray g = L.items[i].checked ? Gray::G1 : Gray::G0;
        if (static_cast<int>(i) == focus_.index)
          canvas_.draw_focus_tile(16, y, kCanvasW - 32, 36, row, Canvas::TextRole::Secondary);
        else
          canvas_.draw_text(24, y + 8, row, Canvas::TextRole::Secondary, g);
      }
    }
  }
}

void App::handle_notes(InputEvent e) {
  ScreenId s = nav_.current();
  if (e == InputEvent::Back) {
    if (s == ScreenId::NotesDetail || s == ScreenId::ListsDetail) {
      nav_.pop();
      after_nav();
    } else {
      nav_.pop();
      after_nav();
    }
    return;
  }
  if (e == InputEvent::PttStart) {
    ptt_active_ = true;
    dirty_ = true;
    return;
  }
  if (e == InputEvent::PttStop) {
    ptt_active_ = false;
    if (!wifi_.connected() && cfg_.stt_path == 0) {
      error_msg_ = "You're offline. Dictation needs Wi-Fi.";
      error_until_ms_ = now_ms_ + 3000;
      dirty_ = true;
      return;
    }
    std::vector<uint8_t> pcm;
    std::string t = cloud_.stt_transcribe(pcm);
    if (t.empty()) {
      error_msg_ = "Couldn't reach speech service. Try again.";
      error_until_ms_ = now_ms_ + 3000;
    } else if (s == ScreenId::NotesDetail && note_index_ < static_cast<int>(data_.notes.size())) {
      data_.notes[note_index_].body += (data_.notes[note_index_].body.empty() ? "" : "\n") + t;
      data_.notes[note_index_].updated_at = now_ms_;
    } else {
      Note n;
      n.id = std::to_string(data_.notes.size() + 1);
      n.title = t.substr(0, 40);
      n.body = t;
      n.created_at = n.updated_at = now_ms_;
      data_.notes.push_back(n);
      note_index_ = static_cast<int>(data_.notes.size()) - 1;
      nav_.replace(ScreenId::NotesDetail);
      after_nav();
      return;
    }
    dirty_ = true;
    return;
  }

  if (s == ScreenId::NotesList) {
    focus_.count = std::max(1, static_cast<int>(data_.notes.size()) + 1);
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index < static_cast<int>(data_.notes.size())) {
        note_index_ = focus_.index;
        nav_.push(ScreenId::NotesDetail);
        after_nav();
      } else {
        Note n;
        n.id = std::to_string(data_.notes.size() + 1);
        n.title = "Note";
        n.body = "";
        n.created_at = n.updated_at = now_ms_;
        data_.notes.push_back(n);
        note_index_ = static_cast<int>(data_.notes.size()) - 1;
        nav_.push(ScreenId::NotesDetail);
        after_nav();
      }
    }
  } else if (s == ScreenId::NotesDetail) {
    focus_.count = 2;
    if (e == InputEvent::Up || e == InputEvent::Down) {
      focus_.move(e == InputEvent::Down ? 1 : -1);
      dirty_ = true;
    } else if (e == InputEvent::Select && focus_.index == 1) {
      if (note_index_ < static_cast<int>(data_.notes.size())) {
        data_.notes.erase(data_.notes.begin() + note_index_);
        nav_.pop();
        after_nav();
      }
    }
  } else if (s == ScreenId::ListsList) {
    // tab switch via long path — short: select
    if (e == InputEvent::Select && data_.lists.empty()) {
      TodoList L;
      L.id = "1";
      L.title = "List";
      data_.lists.push_back(L);
      note_index_ = 0;
      nav_.push(ScreenId::ListsDetail);
      after_nav();
    }
  }
}

void App::render_ledger() {
  draw_status_bar();
  canvas_.draw_text_centered(kCanvasW / 2, 120, "Ledger", Canvas::TextRole::ScreenTitle, Gray::G0);
  canvas_.draw_text_centered(kCanvasW / 2, 200, "Coming soon", Canvas::TextRole::Body, Gray::G0);
  canvas_.draw_text_centered(kCanvasW / 2, 240, "IOU tracking will arrive in a free update.",
                             Canvas::TextRole::Secondary, Gray::G1);
  canvas_.draw_text_centered(kCanvasW / 2, 720, "Press Back for Home", Canvas::TextRole::Secondary, Gray::G1);
  canvas_.draw_focus_tile(16, 660, kCanvasW - 32, 48, "Back", Canvas::TextRole::Body);
}

void App::handle_ledger(InputEvent e) {
  if (e == InputEvent::Back || e == InputEvent::Select || e == InputEvent::Home) {
    go_home();
  }
}

void App::render_clock() {
  draw_status_bar();
  // Tabs
  const char* tabs[] = {"Clock", "Alarms", "Timers"};
  for (int i = 0; i < 3; ++i) {
    int x = 16 + i * 150;
    if (i == clock_tab_)
      canvas_.draw_focus_tile(x, 36, 140, 36, tabs[i], Canvas::TextRole::Secondary);
    else
      canvas_.draw_text(x + 20, 44, tabs[i], Canvas::TextRole::Secondary, Gray::G1);
  }
  ScreenId s = nav_.current();
  if (s == ScreenId::ClockFace || clock_tab_ == 0) {
    int h = 0, m = 0, wd = 0, mo = 0, d = 0;
    clock_.local_hm(h, m, wd, mo, d);
    char tbuf[16];
    if (cfg_.time_format == 24)
      std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", h, m);
    else {
      int h12 = h % 12;
      if (h12 == 0) h12 = 12;
      std::snprintf(tbuf, sizeof(tbuf), "%d:%02d", h12, m);
    }
    canvas_.draw_text_centered(kCanvasW / 2, 280, tbuf, Canvas::TextRole::HugeClock, Gray::G0);
  } else if (clock_tab_ == 1) {
    if (data_.alarms.empty())
      canvas_.draw_text_centered(kCanvasW / 2, 300, "No alarms", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(24, 720, "Add alarm", Canvas::TextRole::Body, Gray::G0);
  } else {
    canvas_.draw_text_centered(kCanvasW / 2, 300, "Timers", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(24, 720, "Start", Canvas::TextRole::Body, Gray::G0);
  }
}

void App::handle_clock(InputEvent e) {
  if (e == InputEvent::Back) {
    nav_.pop();
    after_nav();
    return;
  }
  if (e == InputEvent::Up) {
    clock_tab_ = (clock_tab_ + 2) % 3;
    dirty_ = true;
  } else if (e == InputEvent::Down) {
    clock_tab_ = (clock_tab_ + 1) % 3;
    dirty_ = true;
  }
}

void App::render_pass() {
  if (nav_.current() == ScreenId::PassDetail) {
    // Full-screen QR — status bar hidden
    canvas_.draw_text_centered(kCanvasW / 2, 40, "Pass", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.fill_rect(90, 120, 300, 300, Gray::G0);
    canvas_.fill_rect(110, 140, 260, 260, Gray::G3);
    canvas_.draw_text_centered(kCanvasW / 2, 760, "Back", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }
  draw_status_bar();
  canvas_.draw_text(16, 40, "Pass", Canvas::TextRole::ScreenTitle, Gray::G0);
  if (data_.passes.empty()) {
    canvas_.draw_text_centered(kCanvasW / 2, 280, "No passes yet", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text_centered(kCanvasW / 2, 320, "Add passes from the Pocket companion when available.",
                               Canvas::TextRole::Secondary, Gray::G1);
  } else {
    focus_.count = static_cast<int>(data_.passes.size());
    for (int i = 0; i < focus_.count; ++i) {
      int y = 90 + i * 52;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, data_.passes[i].title, Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, data_.passes[i].title, Canvas::TextRole::Body, Gray::G0);
    }
  }
}

void App::handle_pass(InputEvent e) {
  if (e == InputEvent::Back) {
    if (nav_.current() == ScreenId::PassDetail) {
      nav_.pop();
      after_nav();
    } else {
      nav_.pop();
      after_nav();
    }
    return;
  }
  if (nav_.current() == ScreenId::PassList && e == InputEvent::Select && !data_.passes.empty()) {
    note_index_ = focus_.index;
    nav_.push(ScreenId::PassDetail);
    after_nav();
  }
}

void App::render_weather() {
  draw_status_bar();
  if (data_.weather.city.empty()) {
    canvas_.draw_text_centered(kCanvasW / 2, 280, "Can't load weather", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_focus_tile(16, 400, kCanvasW - 32, 48, "Try again", Canvas::TextRole::Body);
    canvas_.draw_text(16, 480, "Set a city in Settings → Units / Weather.", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }
  canvas_.draw_text(16, 40, data_.weather.city, Canvas::TextRole::ScreenTitle, Gray::G0);
  char tbuf[32];
  std::snprintf(tbuf, sizeof(tbuf), "%d°%c", data_.weather.today_temp, cfg_.weather_units ? 'C' : 'F');
  canvas_.draw_text(16, 100, tbuf, Canvas::TextRole::HugeClock, Gray::G0);
  canvas_.draw_text(16, 180, data_.weather.today_condition, Canvas::TextRole::Body, Gray::G0);
  for (size_t i = 0; i < data_.weather.days.size() && i < 5; ++i) {
    auto& d = data_.weather.days[i];
    char line[64];
    std::snprintf(line, sizeof(line), "%s  %d/%d  %s", d.date.c_str(), d.hi, d.lo, d.condition.c_str());
    canvas_.draw_text(16, 260 + static_cast<int>(i) * 40, line, Canvas::TextRole::Secondary, Gray::G0);
  }
  canvas_.draw_text(16, 760, "Offline · showing saved forecast", Canvas::TextRole::Secondary, Gray::G1);
}

void App::handle_weather(InputEvent e) {
  if (e == InputEvent::Back) {
    nav_.pop();
    after_nav();
  }
}

}  // namespace pocket