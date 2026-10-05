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
      canvas_.draw_focus_tile(kSideMargin, kContentTop, 132, kFocusRowH, "Notes",
                              Canvas::TextRole::Secondary);
      canvas_.draw_text(kSideMargin + 148, kContentTop + kRowTextPad, "Lists",
                        Canvas::TextRole::Secondary, Gray::G1);
    } else {
      canvas_.draw_text(kSideMargin + kRowLabelInset, kContentTop + kRowTextPad, "Notes",
                        Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_focus_tile(kSideMargin + 140, kContentTop, 132, kFocusRowH, "Lists",
                              Canvas::TextRole::Secondary);
    }
  }

  if (s == ScreenId::NotesList) {
    if (data_.notes.empty()) {
      canvas_.draw_text_centered(kCanvasW / 2, kEmptyCenterY, "No notes yet", Canvas::TextRole::Body,
                                 Gray::G0);
      canvas_.draw_text_wrapped(kSideMargin, kEmptyHintY, kContentW, kWrapGap,
                                "Hold the side button to dictate a note",
                                Canvas::TextRole::Secondary, Gray::G1);
    } else {
      focus_.count = static_cast<int>(data_.notes.size()) + 1;
      const int list_top = kContentTop + kTabBand;
      for (int i = 0; i < static_cast<int>(data_.notes.size()); ++i) {
        const int y = row_y(list_top, i);
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, data_.notes[i].title,
                                  Canvas::TextRole::Body);
        else
          canvas_.draw_text_fit(kSideMargin + kRowLabelInset, y + kRowTextPad, kRowLabelW,
                                data_.notes[i].title, Canvas::TextRole::Body, Gray::G0);
      }
    }
    const int y = kBottomCtaY;
    bool fab = focus_.index == static_cast<int>(data_.notes.size()) || data_.notes.empty();
    if (data_.notes.empty()) focus_.count = 1;
    if (fab || focus_.index == static_cast<int>(data_.notes.size()))
      canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, "New note",
                              Canvas::TextRole::Body);
    else
      canvas_.draw_text(kSideMargin + kRowLabelInset, y + kRowTextPad, "New note",
                        Canvas::TextRole::Body, Gray::G0);
  } else if (s == ScreenId::NotesDetail) {
    if (note_index_ >= 0 && note_index_ < static_cast<int>(data_.notes.size())) {
      auto& n = data_.notes[note_index_];
      canvas_.draw_text_fit(kSideMargin, kContentTop, kContentW, n.title, Canvas::TextRole::ScreenTitle,
                            Gray::G0);
      // Body can be several dictated sentences — wrap it, don't clip it to one line.
      // Leave room above the Dictate/Delete stack.
      const int body_bottom = bottom_action_y(1) - kSectionGap;
      const int body_top = below_title(kContentTop);
      canvas_.draw_text_wrapped(kSideMargin, body_top, kContentW, kWrapGap, n.body,
                                Canvas::TextRole::Body, Gray::G0);
      (void)body_bottom;
    }
    focus_.count = 2;
    const char* acts[] = {"Dictate", "Delete"};
    for (int i = 0; i < 2; ++i) {
      const int y = bottom_action_y(1 - i);
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, acts[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + kRowLabelInset, y + kRowTextPad, acts[i], Canvas::TextRole::Body,
                          Gray::G0);
    }
  } else if (s == ScreenId::ListsList) {
    if (data_.lists.empty()) {
      canvas_.draw_text_centered(kCanvasW / 2, kEmptyCenterY, "No lists yet", Canvas::TextRole::Body,
                                 Gray::G0);
    }
    focus_.count = static_cast<int>(data_.lists.size()) + 1;
    const int list_top = kContentTop + kTabBand;
    for (int i = 0; i < static_cast<int>(data_.lists.size()); ++i) {
      const int y = row_y(list_top, i);
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, data_.lists[i].title,
                                Canvas::TextRole::Body);
      else
        canvas_.draw_text_fit(kSideMargin + kRowLabelInset, y + kRowTextPad, kRowLabelW,
                              data_.lists[i].title, Canvas::TextRole::Body, Gray::G0);
    }
    canvas_.draw_text(kSideMargin + kRowLabelInset, kBottomCtaY + kRowTextPad, "New list",
                      Canvas::TextRole::Body, Gray::G0);
  } else if (s == ScreenId::ListsDetail) {
    if (note_index_ < static_cast<int>(data_.lists.size())) {
      auto& L = data_.lists[note_index_];
      canvas_.draw_text_fit(kSideMargin, kContentTop, kContentW, L.title, Canvas::TextRole::ScreenTitle,
                            Gray::G0);
      const int list_top = below_title(kContentTop);
      for (size_t i = 0; i < L.items.size(); ++i) {
        const int y = row_y(list_top, static_cast<int>(i));
        std::string row = (L.items[i].checked ? "[x] " : "[ ] ") + L.items[i].text;
        Gray g = L.items[i].checked ? Gray::G1 : Gray::G0;
        if (static_cast<int>(i) == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, row,
                                  Canvas::TextRole::Secondary);
        else
          canvas_.draw_text_fit(kSideMargin + kRowLabelInset, y + kRowTextPad, kRowLabelW, row,
                                Canvas::TextRole::Secondary, g);
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
    mic_capturing_ = true;
    if (audio_) audio_->start_capture();
    mark_content_dirty();
    return;
  }
  if (e == InputEvent::PttStop) {
    ptt_active_ = false;
    mic_capturing_ = false;
    const MicCaptureResult cap = audio_ ? audio_->stop_capture() : MicCaptureResult{};
    if (!wifi_.connected() && cfg_.stt_path == 0) {
      error_msg_ = "You're offline. Dictation needs Wi-Fi.";
      error_until_ms_ = now_ms_ + 3000;
      mark_content_dirty();
      return;
    }
    if (!cap.ok || cap.pcm.empty()) {
      error_msg_ = "Couldn't hear anything. Hold closer and try again.";
      error_until_ms_ = now_ms_ + 3000;
      mark_content_dirty();
      return;
    }
    std::string t = cloud_.stt_transcribe(cap.pcm);
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
    mark_content_dirty();
    return;
  }

  if (s == ScreenId::NotesList) {
    focus_.count = std::max(1, static_cast<int>(data_.notes.size()) + 1);
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
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
      mark_content_dirty();
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
  canvas_.draw_text(kSideMargin, kContentTop, "Ledger", Canvas::TextRole::ScreenTitle, Gray::G0);
  canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop), kContentW, kWrapGap, "Coming soon",
                            Canvas::TextRole::Body, Gray::G0);
  canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop) + kBodyLinePitch, kContentW, kWrapGap,
                            "IOU tracking will arrive in a free update.", Canvas::TextRole::Secondary,
                            Gray::G1);
  canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kFocusRowW, kFocusRowH, "Back", Canvas::TextRole::Body);
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
    const int x = kSideMargin + i * 148;
    if (i == clock_tab_)
      canvas_.draw_focus_tile(x, kContentTop, 140, kFocusRowH, tabs[i], Canvas::TextRole::Secondary);
    else
      canvas_.draw_text(x + 16, kContentTop + kRowTextPad, tabs[i], Canvas::TextRole::Secondary, Gray::G1);
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
    canvas_.draw_text_centered(kCanvasW / 2, kContentTop + kTabBand + 80, tbuf,
                               Canvas::TextRole::HugeClock, Gray::G0);
  } else if (clock_tab_ == 1) {
    canvas_.draw_text_centered(kCanvasW / 2, kEmptyCenterY, "Coming soon", Canvas::TextRole::Body,
                               Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, kEmptyHintY, kContentW, kWrapGap,
                              "Alarms will arrive in a free update.", Canvas::TextRole::Secondary,
                              Gray::G1);
  } else {
    canvas_.draw_text_centered(kCanvasW / 2, kEmptyCenterY, "Coming soon", Canvas::TextRole::Body,
                               Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, kEmptyHintY, kContentW, kWrapGap,
                              "Timers will arrive in a free update.", Canvas::TextRole::Secondary,
                              Gray::G1);
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
    mark_content_dirty();
  } else if (e == InputEvent::Down) {
    clock_tab_ = (clock_tab_ + 1) % 3;
    mark_content_dirty();
  }
}

void App::render_pass() {
  if (nav_.current() == ScreenId::PassDetail) {
    if (cfg_.parental_hide_pass_share) {
      draw_status_bar();
      canvas_.draw_text(kSideMargin, kContentTop, "Pass", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop), kContentW, kWrapGap, "Pass hidden",
                                Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop) + kBodyLinePitch, kContentW, kWrapGap,
                                "Parental controls hide pass sharing.", Canvas::TextRole::Secondary,
                                Gray::G1);
      canvas_.draw_text_centered(kCanvasW / 2, kFooterY, "Back", Canvas::TextRole::Secondary, Gray::G1);
      return;
    }
    // Full-screen QR — status bar hidden
    canvas_.draw_text(kSideMargin, kContentTop - kStatusBarH + 16, "Pass", Canvas::TextRole::ScreenTitle,
                      Gray::G0);
    constexpr int kQr = 300;
    const int qr_x = (kCanvasW - kQr) / 2;
    const int qr_y = below_title(16) + 24;
    canvas_.fill_rect(qr_x - 20, qr_y - 20, kQr + 40, kQr + 40, Gray::G0);
    canvas_.fill_rect(qr_x, qr_y, kQr, kQr, Gray::G3);
    canvas_.draw_text_centered(kCanvasW / 2, kFooterY, "Back", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }
  draw_status_bar();
  canvas_.draw_text(kSideMargin, kContentTop, "Pass", Canvas::TextRole::ScreenTitle, Gray::G0);
  if (cfg_.parental_hide_pass_share) {
    canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop), kContentW, kWrapGap,
                              "Pass sharing is turned off in parental controls from Pocket Companion.",
                              Canvas::TextRole::Body, Gray::G0);
    return;
  }
  if (data_.passes.empty()) {
    canvas_.draw_text_centered(kCanvasW / 2, kEmptyCenterY, "No passes yet", Canvas::TextRole::Body,
                               Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, kEmptyHintY, kContentW, kWrapGap,
                              "Add passes from the Pocket companion when available.",
                              Canvas::TextRole::Secondary, Gray::G1);
  } else {
    focus_.count = static_cast<int>(data_.passes.size());
    const int list_top = below_title(kContentTop);
    for (int i = 0; i < focus_.count; ++i) {
      const int y = row_y(list_top, i);
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, data_.passes[i].title,
                                Canvas::TextRole::Body);
      else
        canvas_.draw_text_fit(kSideMargin + kRowLabelInset, y + kRowTextPad, kRowLabelW,
                              data_.passes[i].title, Canvas::TextRole::Body, Gray::G0);
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
  if (cfg_.parental_hide_pass_share) return;
  if (nav_.current() == ScreenId::PassList && e == InputEvent::Select && !data_.passes.empty()) {
    note_index_ = focus_.index;
    nav_.push(ScreenId::PassDetail);
    after_nav();
  }
}

void App::render_weather() {
  draw_status_bar();
  if (data_.weather.city.empty()) {
    canvas_.draw_text(kSideMargin, kContentTop, "Weather", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop), kContentW, kWrapGap,
                              "Can't load weather", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, below_title(kContentTop) + kBodyLinePitch, kContentW, kWrapGap,
                              "Set a city in Settings → Units / Weather.", Canvas::TextRole::Secondary,
                              Gray::G1);
    canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kFocusRowW, kFocusRowH, "Try again",
                            Canvas::TextRole::Body);
    return;
  }
  canvas_.draw_text_fit(kSideMargin, kContentTop, kContentW, data_.weather.city,
                        Canvas::TextRole::ScreenTitle, Gray::G0);
  char tbuf[32];
  std::snprintf(tbuf, sizeof(tbuf), "%d°%c", data_.weather.today_temp, cfg_.weather_units ? 'C' : 'F');
  const int temp_y = below_title(kContentTop);
  canvas_.draw_text(kSideMargin, temp_y, tbuf, Canvas::TextRole::HugeClock, Gray::G0);
  const int cond_y = temp_y + canvas_.text_height(Canvas::TextRole::HugeClock) + kSectionGap;
  canvas_.draw_text_fit(kSideMargin, cond_y, kContentW, data_.weather.today_condition,
                        Canvas::TextRole::Body, Gray::G0);
  const int forecast_top = cond_y + kBodyLinePitch + kSectionGap;
  for (size_t i = 0; i < data_.weather.days.size() && i < 5; ++i) {
    auto& d = data_.weather.days[i];
    char line[64];
    std::snprintf(line, sizeof(line), "%s  %d/%d  %s", d.date.c_str(), d.hi, d.lo, d.condition.c_str());
    canvas_.draw_text_fit(kSideMargin, forecast_top + static_cast<int>(i) * kBodyLinePitch, kContentW,
                          line, Canvas::TextRole::Secondary, Gray::G0);
  }
  canvas_.draw_text_fit(kSideMargin, kFooterY, kContentW, "Offline · showing saved forecast",
                        Canvas::TextRole::Secondary, Gray::G1);
}

void App::handle_weather(InputEvent e) {
  if (e == InputEvent::Back) {
    nav_.pop();
    after_nav();
  }
}

}  // namespace pocket
