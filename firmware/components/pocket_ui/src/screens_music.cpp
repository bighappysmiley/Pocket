#include "pocket/app.hpp"
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

/** Minimal JSON array parse for [{id,title,filename,size},...] — tolerant of spacing. */
void parse_music_list(const std::string& json, std::vector<MusicTrack>& out) {
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

    MusicTrack t;
    grab("id", t.id);
    grab("title", t.title);
    grab("filename", t.filename);
    const size_t sk = obj_s.find("\"size");
    if (sk != std::string::npos) {
      size_t n = obj_s.find(':', sk);
      if (n != std::string::npos) {
        ++n;
        while (n < obj_s.size() && (obj_s[n] == ' ' || obj_s[n] == '\t')) ++n;
        t.size_bytes = std::atoi(obj_s.c_str() + n);
      }
    }
    if (t.id.empty()) {
      i = end + 1;
      continue;
    }
    if (t.title.empty()) t.title = t.filename.empty() ? t.id : t.filename;
    out.push_back(t);
    i = end + 1;
  }
}

}  // namespace

void App::music_sync_from_cloud() {
  music_status_ = "Syncing…";
  mark_content_dirty();
  if (!wifi_.connected()) {
    music_status_ = "Wi-Fi required to sync.";
    return;
  }
  if (!storage_ || !storage_->music_ensure_root()) {
    music_status_ = "No storage (need SD or free internal).";
    return;
  }
  const std::string root = storage_->music_root();
  const std::string json = cloud_.music_list_json(cfg_.device_id);
  std::vector<MusicTrack> remote;
  parse_music_list(json, remote);
  if (remote.empty() && json.find('[') != std::string::npos) {
    music_tracks_.clear();
    music_status_ = "Library empty — upload in Companion.";
    return;
  }
  std::vector<MusicTrack> local;
  for (auto& t : remote) {
    const uint64_t free = storage_->free_bytes(root);
    if (t.size_bytes > 0 && free < static_cast<uint64_t>(t.size_bytes) + 64 * 1024) {
      music_status_ = "Not enough free space.";
      break;
    }
    std::string fname = t.filename.empty() ? (t.id + ".wav") : t.filename;
    // Sanitize path segment
    for (char& c : fname) {
      if (c == '/' || c == '\\') c = '_';
    }
    const std::string path = root + "/" + fname;
    struct stat st {};
    if (stat(path.c_str(), &st) == 0 && st.st_size > 0) {
      t.local_path = path;
      local.push_back(t);
      continue;
    }
    auto bytes = cloud_.music_download(cfg_.device_id, t.id);
    if (bytes.empty()) {
      music_status_ = "Download failed for " + t.title;
      continue;
    }
    if (!write_bytes(path, bytes)) {
      music_status_ = "Couldn't write " + fname;
      continue;
    }
    t.local_path = path;
    t.size_bytes = static_cast<int>(bytes.size());
    local.push_back(t);
  }
  music_tracks_ = std::move(local);
  music_status_ = music_tracks_.empty() ? "No tracks available." : ("Synced " + std::to_string(music_tracks_.size()));
}

void App::render_music() {
  draw_status_bar();
  const ScreenId s = nav_.current();
  if (s == ScreenId::MusicNowPlaying) {
    canvas_.draw_text(kSideMargin, kContentTop, "Now playing", Canvas::TextRole::ScreenTitle, Gray::G0);
    if (music_index_ >= 0 && music_index_ < static_cast<int>(music_tracks_.size())) {
      const auto& t = music_tracks_[music_index_];
      canvas_.draw_text_wrapped(kSideMargin, kContentTop + kTitleToBody, kContentW, 6, t.title,
                                Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, kContentTop + kTitleToBody + 2 * kBodyLinePitch,
                        music_playing_ ? "Playing…" : "Stopped", Canvas::TextRole::Secondary, Gray::G1);
    }
    focus_.count = 2;
    const char* acts[] = {"Stop", "Back"};
    for (int i = 0; i < 2; ++i) {
      const int y = kBottomCtaY - (1 - i) * kRowPitch;
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + 12, acts[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  canvas_.draw_text(kSideMargin, kContentTop, "Music", Canvas::TextRole::ScreenTitle, Gray::G0);
  if (!music_status_.empty()) {
    canvas_.draw_text_fit(kSideMargin, kContentTop + 48, kContentW, music_status_, Canvas::TextRole::Secondary,
                          Gray::G1);
  }
  const int list_top = kContentTop + 88;
  const int n = static_cast<int>(music_tracks_.size());
  focus_.count = n + 2;  // tracks + Sync + Back
  for (int i = 0; i < n && i < 8; ++i) {
    const int y = list_top + i * kRowPitch;
    const char* label = music_tracks_[i].title.c_str();
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

void App::handle_music(InputEvent e) {
  const ScreenId s = nav_.current();
  if (s == ScreenId::MusicNowPlaying) {
    focus_.count = 2;
    if (e == InputEvent::Back || (e == InputEvent::Select && focus_.index == 1)) {
      if (audio_) audio_->stop();
      music_playing_ = false;
      nav_.pop();
      after_nav();
      return;
    }
    if (e == InputEvent::Up || e == InputEvent::Down) {
      focus_.move(e == InputEvent::Down ? 1 : -1);
      mark_content_dirty();
    } else if (e == InputEvent::Select && focus_.index == 0) {
      if (audio_) audio_->stop();
      music_playing_ = false;
      mark_content_dirty();
    }
    return;
  }

  const int n = static_cast<int>(music_tracks_.size());
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
      music_index_ = focus_.index;
      const auto& t = music_tracks_[music_index_];
      if (t.local_path.empty()) {
        music_status_ = "Sync first.";
        mark_content_dirty();
        return;
      }
      if (audio_ && audio_->play_file(t.local_path)) {
        music_playing_ = true;
        play_sound(SoundId::Click);
        nav_.push(ScreenId::MusicNowPlaying);
        after_nav();
      } else {
        music_status_ = "Couldn't play (need WAV).";
        mark_content_dirty();
      }
    } else if (focus_.index == n) {
      music_sync_from_cloud();
      mark_content_dirty();
    } else {
      nav_.pop();
      after_nav();
    }
  }
}

}  // namespace pocket
