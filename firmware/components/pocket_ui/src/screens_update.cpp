#include "pocket/app.hpp"
#include <cstdio>
#include <cstring>

namespace pocket {
namespace {

std::string json_field(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t p = json.find(needle);
  if (p == std::string::npos) return {};
  p = json.find(':', p + needle.size());
  if (p == std::string::npos) return {};
  while (p + 1 < json.size() && (json[p + 1] == ' ' || json[p + 1] == '\t')) ++p;
  if (p + 1 >= json.size()) return {};
  if (json[p + 1] == '"') {
    size_t start = p + 2;
    size_t end = json.find('"', start);
    if (end == std::string::npos) return {};
    return json.substr(start, end - start);
  }
  size_t start = p + 1;
  size_t end = start;
  while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') ++end;
  return json.substr(start, end - start);
}

}  // namespace

bool App::parse_firmware_latest(const std::string& json, FirmwareUpdateInfo* out) {
  if (!out || json.empty()) return false;
  out->build_id = json_field(json, "build_id");
  out->version = json_field(json, "version");
  out->url = json_field(json, "url");
  const std::string sz = json_field(json, "size");
  out->size_bytes = 0;
  if (!sz.empty()) out->size_bytes = std::atoi(sz.c_str());
  if (out->url.empty()) return false;
  return true;
}

void App::begin_firmware_update() {
  ota_status_ = "Checking for update…";
  nav_.replace(ScreenId::SettingsUpdateProgress);
  after_nav();

  if (!wifi_.connected()) {
    ota_status_ = "Connect to Wi‑Fi first.";
    nav_.replace(ScreenId::SettingsUpdateResult);
    after_nav();
    return;
  }

  const std::string meta = cloud_.firmware_latest_json();
  FirmwareUpdateInfo info;
  if (!parse_firmware_latest(meta, &info)) {
    ota_status_ = "Couldn't reach update server.";
    nav_.replace(ScreenId::SettingsUpdateResult);
    after_nav();
    return;
  }

  if (!info.build_id.empty() && info.build_id == cfg_.fw_build_id) {
    ota_status_ = "You're up to date.";
    nav_.replace(ScreenId::SettingsUpdateResult);
    after_nav();
    return;
  }

  if (!ota_) {
    ota_status_ = "Update not available on this build.";
    nav_.replace(ScreenId::SettingsUpdateResult);
    after_nav();
    return;
  }

  ota_status_ = "Downloading update…";
  mark_content_dirty();
  if (dirty_) {
    present_canvas(false);
    dirty_ = false;
  }

  std::string st;
  const bool ok = ota_->apply_https_ota(info.url, &st);
  ota_status_ = st.empty() ? (ok ? "Update installed." : "Couldn't install update.") : st;
  // apply_https_ota restarts on success; if we return, show result.
  nav_.replace(ScreenId::SettingsUpdateResult);
  after_nav();
}

}  // namespace pocket
