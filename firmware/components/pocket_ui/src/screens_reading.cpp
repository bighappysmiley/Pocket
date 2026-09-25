#include "pocket/app.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sys/stat.h>

namespace pocket {
namespace {

bool write_bytes(const std::string& path, const std::vector<uint8_t>& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(out);
}

/** Minimal JSON array parse for [{id,title,author,format,filename,size},...] — tolerant of spacing. */
void parse_books_list(const std::string& json, std::vector<Book>& out) {
  out.clear();
  size_t i = 0;
  while (i < json.size()) {
    const size_t obj = json.find('{', i);
    if (obj == std::string::npos) break;
    const size_t end = json.find('}', obj + 1);
    if (end == std::string::npos) break;
    const std::string obj_s = json.substr(obj, end - obj + 1);

    auto grab = [&](const char* key, std::string& dest) {
      const std::string needle = std::string("\"") + key + "\"";
      const size_t k = obj_s.find(needle);
      if (k == std::string::npos) return;
      const size_t colon = obj_s.find(':', k + needle.size());
      if (colon == std::string::npos) return;
      const size_t q1 = obj_s.find('"', colon + 1);
      if (q1 == std::string::npos) return;
      const size_t q2 = obj_s.find('"', q1 + 1);
      if (q2 == std::string::npos) return;
      dest = obj_s.substr(q1 + 1, q2 - q1 - 1);
    };

    Book b;
    grab("id", b.id);
    grab("title", b.title);
    grab("author", b.author);
    grab("format", b.format);
    grab("filename", b.filename);
    const size_t sk = obj_s.find("\"size");
    if (sk != std::string::npos) {
      size_t n = obj_s.find(':', sk);
      if (n != std::string::npos) {
        ++n;
        while (n < obj_s.size() && (obj_s[n] == ' ' || obj_s[n] == '\t')) ++n;
        b.size_bytes = std::atoi(obj_s.c_str() + n);
      }
    }
    if (b.id.empty()) {
      i = end + 1;
      continue;
    }
    if (b.title.empty()) b.title = b.filename.empty() ? b.id : b.filename;
    out.push_back(b);
    i = end + 1;
  }
}

}  // namespace

void App::reading_sync_from_cloud() {
  reading_status_ = "Syncing…";
  mark_content_dirty();
  if (!wifi_.connected()) {
    reading_status_ = "Wi-Fi required to sync.";
    return;
  }
  if (!storage_ || !storage_->book_ensure_root()) {
    reading_status_ = "No storage (need SD or free internal).";
    return;
  }
  const std::string root = storage_->book_root();
  const std::string json = cloud_.books_list_json(cfg_.device_id);
  std::vector<Book> remote;
  parse_books_list(json, remote);
  if (remote.empty() && json.find('[') != std::string::npos) {
    books_.clear();
    reading_status_ = "Library empty — upload in Companion.";
    return;
  }
  std::vector<Book> local;
  for (auto& b : remote) {
    const uint64_t free = storage_->free_bytes(root);
    if (b.size_bytes > 0 && free < static_cast<uint64_t>(b.size_bytes) + 64 * 1024) {
      reading_status_ = "Not enough free space.";
      break;
    }
    std::string fname = b.filename.empty() ? (b.id + ".txt") : b.filename;
    for (char& c : fname) {
      if (c == '/' || c == '\\') c = '_';
    }
    if (!fname.empty() && fname.substr(fname.size() >= 4 ? fname.size() - 4 : 0) != ".txt") {
      fname += ".txt";  // device always stores normalized plain text
    }
    const std::string path = root + "/" + fname;
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0) {
      b.local_path = path;
      local.push_back(b);
      continue;
    }
    auto bytes = cloud_.book_download(cfg_.device_id, b.id);
    if (bytes.empty()) {
      reading_status_ = "Download failed for " + b.title;
      continue;
    }
    if (!write_bytes(path, bytes)) {
      reading_status_ = "Couldn't write " + fname;
      continue;
    }
    b.local_path = path;
    b.size_bytes = static_cast<int>(bytes.size());
    local.push_back(b);
  }
  books_ = std::move(local);
  reading_status_ = books_.empty() ? "No books available." : "Synced " + std::to_string(books_.size());
}

void App::reading_paginate() {
  reading_pages_.clear();
  reading_pages_.push_back(0);
  const std::string& text = reading_text_;
  const size_t n = text.size();
  if (n == 0) return;

  const int max_w = kContentW;
  const int line_h = canvas_.text_height(Canvas::TextRole::Body) + 8;
  const int top = kContentTop + kTitleToBody;
  const int bottom = kBottomCtaY - kRowPitch - 8;
  const int lines_per_page = std::max(1, (bottom - top) / std::max(1, line_h));

  size_t i = 0;
  int line_count = 0;
  auto advance_line = [&]() {
    ++line_count;
    if (line_count >= lines_per_page) {
      line_count = 0;
      if (i < n) reading_pages_.push_back(i);
    }
  };

  while (i < n) {
    while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
    if (i >= n) break;
    if (text[i] == '\n') {
      ++i;
      advance_line();
      continue;
    }
    const size_t line_start = i;
    size_t last_break = i;
    size_t j = i;
    while (j < n && text[j] != '\n') {
      size_t word_end = j;
      while (word_end < n && text[word_end] != ' ' && text[word_end] != '\t' && text[word_end] != '\n') {
        ++word_end;
      }
      const std::string_view candidate(text.data() + line_start, word_end - line_start);
      if (canvas_.text_width(candidate, Canvas::TextRole::Body) <= max_w) {
        last_break = word_end;
        j = word_end;
        while (j < n && (text[j] == ' ' || text[j] == '\t')) ++j;
      } else if (last_break == line_start) {
        size_t cut = line_start + 1;
        while (cut < word_end &&
              canvas_.text_width(std::string_view(text.data() + line_start, cut - line_start),
                                  Canvas::TextRole::Body) <= max_w) {
          ++cut;
        }
        if (cut > line_start + 1) --cut;
        last_break = cut;
        j = cut;
        break;
      } else {
        break;
      }
    }
    if (last_break > line_start) {
      i = last_break;
      while (i < n && (text[i] == ' ' || text[i] == '\t')) ++i;
      advance_line();
    } else if (j < n && text[j] == '\n') {
      i = j + 1;
      advance_line();
    } else {
      break;
    }
  }
}

void App::reading_open_book(int index) {
  if (index < 0 || index >= static_cast<int>(books_.size())) return;
  const auto& b = books_[index];
  if (b.local_path.empty()) {
    reading_status_ = "Sync first.";
    mark_content_dirty();
    return;
  }
  std::ifstream in(b.local_path, std::ios::binary);
  if (!in) {
    reading_status_ = "Couldn't open that book.";
    mark_content_dirty();
    return;
  }
  constexpr size_t kMaxBookBytes = 3 * 1024 * 1024;  // generous for a normalized-text novel
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (text.size() > kMaxBookBytes) text.resize(kMaxBookBytes);
  reading_text_ = std::move(text);
  reading_paginate();
  reading_page_ = 0;
  if (cfg_.reading_last_book_id == b.id && cfg_.reading_last_page < static_cast<int>(reading_pages_.size())) {
    reading_page_ = cfg_.reading_last_page;
  }
  book_index_ = index;
  play_sound(SoundId::Click);
  nav_.push(ScreenId::ReadingBook);
  after_nav();
}

void App::render_reading() {
  draw_status_bar();
  const ScreenId s = nav_.current();

  if (s == ScreenId::ReadingBook) {
    if (book_index_ < 0 || book_index_ >= static_cast<int>(books_.size()) || reading_pages_.empty()) {
      canvas_.draw_text(kSideMargin, kContentTop, "Reading", Canvas::TextRole::ScreenTitle, Gray::G0);
      focus_.count = 1;
      canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kCanvasW - 32, kFocusRowH, "Back",
                              Canvas::TextRole::Body);
      return;
    }
    const auto& b = books_[book_index_];
    canvas_.draw_text_fit(kSideMargin, kContentTop, kContentW, b.title, Canvas::TextRole::ScreenTitle,
                          Gray::G0);
    const size_t start = reading_pages_[static_cast<size_t>(reading_page_)];
    const size_t stop = (static_cast<size_t>(reading_page_) + 1 < reading_pages_.size())
                            ? reading_pages_[static_cast<size_t>(reading_page_) + 1]
                            : reading_text_.size();
    const std::string page_text = reading_text_.substr(start, stop - start);
    canvas_.draw_text_wrapped(kSideMargin, kContentTop + kTitleToBody, kContentW, 8, page_text,
                              Canvas::TextRole::Body, Gray::G0);
    char pg[48];
    std::snprintf(pg, sizeof(pg), "Page %d of %d", reading_page_ + 1, static_cast<int>(reading_pages_.size()));
    canvas_.draw_text(kSideMargin, kBottomCtaY - kRowPitch - 28, pg, Canvas::TextRole::Secondary, Gray::G1);

    focus_.count = 3;
    const char* acts[] = {"Prev page", "Next page", "Back"};
    for (int i = 0; i < 3; ++i) {
      const int y = kBottomCtaY - (2 - i) * kRowPitch;
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + 12, acts[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  canvas_.draw_text(kSideMargin, kContentTop, "Reading", Canvas::TextRole::ScreenTitle, Gray::G0);
  if (!reading_status_.empty()) {
    canvas_.draw_text_fit(kSideMargin, kContentTop + 48, kContentW, reading_status_, Canvas::TextRole::Secondary,
                          Gray::G1);
  }
  const int list_top = kContentTop + 88;
  const int n = static_cast<int>(books_.size());
  focus_.count = n + 2;  // books + Sync + Back
  for (int i = 0; i < n && i < 8; ++i) {
    const int y = list_top + i * kRowPitch;
    std::string label = books_[i].title;
    if (!books_[i].format.empty()) label += " · " + books_[i].format;
    if (i == focus_.index)
      canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, label, Canvas::TextRole::Body);
    else
      canvas_.draw_text_fit(kSideMargin + 8, y + 12, kCanvasW - 48, label, Canvas::TextRole::Body, Gray::G0);
  }
  {
    const int y = list_top + std::min(n, 8) * kRowPitch;
    if (focus_.index == n)
      canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, "Sync from Companion",
                              Canvas::TextRole::Body);
    else
      canvas_.draw_text(kSideMargin + 8, y + 12, "Sync from Companion", Canvas::TextRole::Body, Gray::G0);
    const int y2 = y + kRowPitch;
    if (focus_.index == n + 1)
      canvas_.draw_focus_tile(kSideMargin, y2, kCanvasW - 32, kFocusRowH, "Back", Canvas::TextRole::Body);
    else
      canvas_.draw_text(kSideMargin + 8, y2 + 12, "Back", Canvas::TextRole::Body, Gray::G0);
  }
}

void App::handle_reading(InputEvent e) {
  const ScreenId s = nav_.current();

  if (s == ScreenId::ReadingBook) {
    focus_.count = 3;
    // Persist progress on any exit from the book so a reboot resumes here.
    auto save_progress = [&]() {
      if (book_index_ >= 0 && book_index_ < static_cast<int>(books_.size())) {
        cfg_.reading_last_book_id = books_[book_index_].id;
        cfg_.reading_last_page = reading_page_;
        store_.save(cfg_);
      }
    };
    if (e == InputEvent::Back) {
      save_progress();
      nav_.pop();
      after_nav();
      return;
    }
    if (e == InputEvent::Up || e == InputEvent::Down) {
      focus_.move(e == InputEvent::Down ? 1 : -1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        if (reading_page_ > 0) {
          --reading_page_;
          save_progress();
          mark_content_dirty();
        }
      } else if (focus_.index == 1) {
        if (reading_page_ + 1 < static_cast<int>(reading_pages_.size())) {
          ++reading_page_;
          save_progress();
          mark_content_dirty();
        }
      } else {
        save_progress();
        nav_.pop();
        after_nav();
      }
    }
    return;
  }

  const int n = static_cast<int>(books_.size());
  focus_.count = n + 2;
  if (e == InputEvent::Back) {
    nav_.pop();
    after_nav();
    return;
  }
  if (e == InputEvent::Up) {
    focus_.move(-1);
    mark_content_dirty();
  } else if (e == InputEvent::Down) {
    focus_.move(1);
    mark_content_dirty();
  } else if (e == InputEvent::Select) {
    if (focus_.index < n) {
      reading_open_book(focus_.index);
    } else if (focus_.index == n) {
      reading_sync_from_cloud();
      mark_content_dirty();
    } else {
      nav_.pop();
      after_nav();
    }
  }
}

}  // namespace pocket
