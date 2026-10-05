#include "pocket/app.hpp"
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>

namespace pocket {
namespace {

std::string json_escape(const std::string& s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') {
      o.push_back('\\');
      o.push_back(static_cast<char>(c));
    } else if (c == '\n') {
      o += "\\n";
    } else if (c == '\r') {
      o += "\\r";
    } else if (c < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      o += buf;
    } else {
      o.push_back(static_cast<char>(c));
    }
  }
  return o;
}

std::string json_unescape(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      if (n == 'n')
        o.push_back('\n');
      else if (n == 'r')
        o.push_back('\r');
      else if (n == 't')
        o.push_back('\t');
      else if (n == 'u' && i + 4 < s.size()) {
        // Skip simple \u00XX ascii escapes.
        unsigned v = 0;
        for (int k = 0; k < 4; ++k) {
          const char h = s[++i];
          v <<= 4;
          if (h >= '0' && h <= '9')
            v |= static_cast<unsigned>(h - '0');
          else if (h >= 'a' && h <= 'f')
            v |= static_cast<unsigned>(h - 'a' + 10);
          else if (h >= 'A' && h <= 'F')
            v |= static_cast<unsigned>(h - 'A' + 10);
        }
        if (v < 128) o.push_back(static_cast<char>(v));
      } else {
        o.push_back(n);
      }
    } else {
      o.push_back(s[i]);
    }
  }
  return o;
}

bool grab_json_string(const std::string& obj, const char* key, std::string& dest) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t k = obj.find(needle);
  if (k == std::string::npos) return false;
  size_t colon = obj.find(':', k + needle.size());
  if (colon == std::string::npos) return false;
  size_t q1 = obj.find('"', colon + 1);
  if (q1 == std::string::npos) return false;
  std::string raw;
  for (size_t i = q1 + 1; i < obj.size(); ++i) {
    if (obj[i] == '\\' && i + 1 < obj.size()) {
      raw.push_back(obj[i]);
      raw.push_back(obj[++i]);
      continue;
    }
    if (obj[i] == '"') {
      dest = json_unescape(raw);
      return true;
    }
    raw.push_back(obj[i]);
  }
  return false;
}

bool grab_json_int64(const std::string& obj, const char* key, int64_t& dest) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t k = obj.find(needle);
  if (k == std::string::npos) return false;
  size_t colon = obj.find(':', k + needle.size());
  if (colon == std::string::npos) return false;
  size_t i = colon + 1;
  while (i < obj.size() && (obj[i] == ' ' || obj[i] == '\t')) ++i;
  dest = static_cast<int64_t>(std::atoll(obj.c_str() + i));
  return true;
}

}  // namespace

void App::load_local_notes() {
  data_.notes.clear();
  if (!storage_ || !storage_->data_ensure_root()) return;
  const std::string path = storage_->data_root() + "/notes/index.json";
  std::ifstream in(path);
  if (!in) return;
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  size_t i = 0;
  while (i < json.size()) {
    const size_t obj = json.find('{', i);
    if (obj == std::string::npos) break;
    const size_t end = json.find('}', obj + 1);
    if (end == std::string::npos) break;
    const std::string obj_s = json.substr(obj, end - obj + 1);
    Note n;
    grab_json_string(obj_s, "id", n.id);
    grab_json_string(obj_s, "title", n.title);
    grab_json_string(obj_s, "body", n.body);
    grab_json_int64(obj_s, "created_at", n.created_at);
    grab_json_int64(obj_s, "updated_at", n.updated_at);
    if (!n.id.empty()) data_.notes.push_back(n);
    i = end + 1;
  }
}

void App::save_local_notes() {
  if (!storage_ || !storage_->data_ensure_root()) return;
  const std::string path = storage_->data_root() + "/notes/index.json";
  std::ofstream out(path, std::ios::trunc);
  if (!out) return;
  out << "[";
  for (size_t i = 0; i < data_.notes.size(); ++i) {
    const auto& n = data_.notes[i];
    if (i) out << ",";
    out << "{\"id\":\"" << json_escape(n.id) << "\",\"title\":\"" << json_escape(n.title)
        << "\",\"body\":\"" << json_escape(n.body) << "\",\"created_at\":" << n.created_at
        << ",\"updated_at\":" << n.updated_at << "}";
  }
  out << "]";
}

void App::load_local_lists() {
  data_.lists.clear();
  if (!storage_ || !storage_->data_ensure_root()) return;
  const std::string path = storage_->data_root() + "/lists/index.json";
  std::ifstream in(path);
  if (!in) return;
  std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  // Minimal: [{id,title,items:[{text,checked},...]}]
  size_t i = 0;
  while (i < json.size()) {
    const size_t obj = json.find("{\"id\"", i);
    if (obj == std::string::npos) {
      const size_t obj2 = json.find('{', i);
      if (obj2 == std::string::npos) break;
      // fall through with obj2
    }
    const size_t start = (obj == std::string::npos) ? json.find('{', i) : obj;
    if (start == std::string::npos) break;
    // Find matching end by scanning for items array end — tolerant parser.
    size_t end = start + 1;
    int depth = 1;
    while (end < json.size() && depth > 0) {
      if (json[end] == '{') ++depth;
      else if (json[end] == '}') --depth;
      ++end;
    }
    const std::string obj_s = json.substr(start, end - start);
    TodoList L;
    grab_json_string(obj_s, "id", L.id);
    grab_json_string(obj_s, "title", L.title);
    size_t items = obj_s.find("\"items\"");
    if (items != std::string::npos) {
      size_t arr = obj_s.find('[', items);
      size_t arr_end = obj_s.rfind(']');
      if (arr != std::string::npos && arr_end != std::string::npos && arr_end > arr) {
        size_t p = arr + 1;
        while (p < arr_end) {
          size_t io = obj_s.find('{', p);
          if (io == std::string::npos || io >= arr_end) break;
          size_t ie = obj_s.find('}', io + 1);
          if (ie == std::string::npos || ie > arr_end) break;
          const std::string item = obj_s.substr(io, ie - io + 1);
          ListItem it;
          grab_json_string(item, "text", it.text);
          if (item.find("\"checked\":true") != std::string::npos ||
              item.find("\"checked\": true") != std::string::npos)
            it.checked = true;
          if (!it.text.empty()) L.items.push_back(it);
          p = ie + 1;
        }
      }
    }
    if (!L.id.empty()) data_.lists.push_back(L);
    i = end;
  }
}

void App::save_local_lists() {
  if (!storage_ || !storage_->data_ensure_root()) return;
  const std::string path = storage_->data_root() + "/lists/index.json";
  std::ofstream out(path, std::ios::trunc);
  if (!out) return;
  out << "[";
  for (size_t i = 0; i < data_.lists.size(); ++i) {
    const auto& L = data_.lists[i];
    if (i) out << ",";
    out << "{\"id\":\"" << json_escape(L.id) << "\",\"title\":\"" << json_escape(L.title)
        << "\",\"items\":[";
    for (size_t j = 0; j < L.items.size(); ++j) {
      if (j) out << ",";
      out << "{\"text\":\"" << json_escape(L.items[j].text)
          << "\",\"checked\":" << (L.items[j].checked ? "true" : "false") << "}";
    }
    out << "]}";
  }
  out << "]";
}

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
      const int body_top = below_title(kContentTop);
      canvas_.draw_text_wrapped(kSideMargin, body_top, kContentW, kWrapGap, n.body,
                                Canvas::TextRole::Body, Gray::G0);
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
    if (ptt_active_) {
      canvas_.draw_text_centered(kCanvasW / 2, bottom_action_y(1) - 36, "Listening…",
                                 Canvas::TextRole::Secondary, Gray::G0);
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
    const int y = kBottomCtaY;
    if (focus_.index == static_cast<int>(data_.lists.size()) || data_.lists.empty()) {
      if (data_.lists.empty()) focus_.count = 1;
      canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, "New list", Canvas::TextRole::Body);
    } else {
      canvas_.draw_text(kSideMargin + kRowLabelInset, y + kRowTextPad, "New list",
                        Canvas::TextRole::Body, Gray::G0);
    }
  } else if (s == ScreenId::ListsDetail) {
    if (note_index_ < static_cast<int>(data_.lists.size())) {
      auto& L = data_.lists[note_index_];
      canvas_.draw_text_fit(kSideMargin, kContentTop, kContentW, L.title, Canvas::TextRole::ScreenTitle,
                            Gray::G0);
      const int list_top = below_title(kContentTop);
      focus_.count = static_cast<int>(L.items.size()) + 1;  // items + Add via dictate
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
      const int y = kBottomCtaY;
      if (focus_.index == static_cast<int>(L.items.size()))
        canvas_.draw_focus_tile(kSideMargin, y, kFocusRowW, kFocusRowH, "Hold BOOT to add item",
                                Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + kRowLabelInset, y + kRowTextPad, "Hold BOOT to add item",
                          Canvas::TextRole::Secondary, Gray::G1);
    }
  }

  if (now_ms_ < error_until_ms_ && !error_msg_.empty()) {
    canvas_.fill_rect(0, kFooterY - 8, kCanvasW, 44, Gray::G3);
    canvas_.draw_text_wrapped(kSideMargin, kFooterY - 4, kContentW, kWrapGap, error_msg_,
                              Canvas::TextRole::Secondary, Gray::G0);
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

  // Tab switch Notes ↔ Lists from either list root.
  if ((s == ScreenId::NotesList || s == ScreenId::ListsList) &&
      (e == InputEvent::Up || e == InputEvent::Down) && focus_.index == 0 && data_.notes.empty() &&
      data_.lists.empty() && e == InputEvent::Down) {
    // fall through to normal focus below when empty — still allow tab via Select on tab labels
  }
  if ((s == ScreenId::NotesList || s == ScreenId::ListsList) && e == InputEvent::Select &&
      focus_.count <= 1 && notes_tab_ == (s == ScreenId::NotesList ? 0 : 1)) {
    // Allow switching tabs: when on Notes tab with empty/fab, long-press Home is separate;
    // use Function while on "New note" doesn't switch. Explicit: Up from index 0 cycles tabs.
  }
  if ((s == ScreenId::NotesList || s == ScreenId::ListsList) && e == InputEvent::Up &&
      focus_.index == 0) {
    // Toggle tab when pressing Up on the first row / empty FAB.
    if (s == ScreenId::NotesList) {
      notes_tab_ = 1;
      focus_.index = 0;
      nav_.replace(ScreenId::ListsList);
      after_nav();
      return;
    }
    notes_tab_ = 0;
    focus_.index = 0;
    nav_.replace(ScreenId::NotesList);
    after_nav();
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
      mark_content_dirty();
      return;
    }
    error_msg_.clear();
    if (s == ScreenId::ListsDetail && note_index_ < static_cast<int>(data_.lists.size())) {
      ListItem it;
      it.text = t;
      data_.lists[note_index_].items.push_back(it);
      save_local_lists();
    } else if (s == ScreenId::NotesDetail && note_index_ < static_cast<int>(data_.notes.size())) {
      data_.notes[note_index_].body += (data_.notes[note_index_].body.empty() ? "" : "\n") + t;
      data_.notes[note_index_].updated_at = now_ms_;
      if (data_.notes[note_index_].title == "Note" || data_.notes[note_index_].title.empty()) {
        data_.notes[note_index_].title = t.substr(0, 40);
      }
      save_local_notes();
    } else {
      Note n;
      n.id = std::to_string(now_ms_) + "-" + std::to_string(data_.notes.size() + 1);
      n.title = t.substr(0, 40);
      n.body = t;
      n.created_at = n.updated_at = now_ms_;
      data_.notes.push_back(n);
      note_index_ = static_cast<int>(data_.notes.size()) - 1;
      save_local_notes();
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
        n.id = std::to_string(now_ms_) + "-" + std::to_string(data_.notes.size() + 1);
        n.title = "Note";
        n.body = "";
        n.created_at = n.updated_at = now_ms_;
        data_.notes.push_back(n);
        note_index_ = static_cast<int>(data_.notes.size()) - 1;
        save_local_notes();
        nav_.push(ScreenId::NotesDetail);
        after_nav();
      }
    }
  } else if (s == ScreenId::NotesDetail) {
    focus_.count = 2;
    if (e == InputEvent::Up || e == InputEvent::Down) {
      focus_.move(e == InputEvent::Down ? 1 : -1);
      mark_content_dirty();
    } else if (e == InputEvent::Select && focus_.index == 0) {
      error_msg_ = "Hold the side button to dictate.";
      error_until_ms_ = now_ms_ + 2500;
      mark_content_dirty();
    } else if (e == InputEvent::Select && focus_.index == 1) {
      if (note_index_ < static_cast<int>(data_.notes.size())) {
        data_.notes.erase(data_.notes.begin() + note_index_);
        save_local_notes();
        nav_.pop();
        after_nav();
      }
    }
  } else if (s == ScreenId::ListsList) {
    focus_.count = std::max(1, static_cast<int>(data_.lists.size()) + 1);
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index < static_cast<int>(data_.lists.size())) {
        note_index_ = focus_.index;
        nav_.push(ScreenId::ListsDetail);
        after_nav();
      } else {
        TodoList L;
        L.id = std::to_string(now_ms_) + "-L" + std::to_string(data_.lists.size() + 1);
        L.title = "List";
        data_.lists.push_back(L);
        note_index_ = static_cast<int>(data_.lists.size()) - 1;
        save_local_lists();
        nav_.push(ScreenId::ListsDetail);
        after_nav();
      }
    }
  } else if (s == ScreenId::ListsDetail) {
    if (note_index_ < static_cast<int>(data_.lists.size())) {
      auto& L = data_.lists[note_index_];
      focus_.count = static_cast<int>(L.items.size()) + 1;
      if (e == InputEvent::Up || e == InputEvent::Down) {
        focus_.move(e == InputEvent::Down ? 1 : -1);
        mark_content_dirty();
      } else if (e == InputEvent::Select && focus_.index < static_cast<int>(L.items.size())) {
        L.items[static_cast<size_t>(focus_.index)].checked =
            !L.items[static_cast<size_t>(focus_.index)].checked;
        save_local_lists();
        mark_content_dirty();
      }
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
