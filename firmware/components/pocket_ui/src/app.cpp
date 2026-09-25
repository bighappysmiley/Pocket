#include "pocket/app.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

namespace pocket {

extern void draw_status_bar_impl(Canvas& c, const DeviceConfig& cfg, int hour, int minute, bool wifi_ok,
                                 int battery_pct, bool time_ok);

static const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static const char* kTimezones[] = {"America/New_York", "America/Chicago", "America/Denver",
                                   "America/Los_Angeles", "America/Phoenix", "UTC", "Europe/London"};

App::App(ConfigStore& store, PlatformClock& clock, PlatformWifi& wifi, PlatformCloud& cloud,
         PlatformDisplay& display, PlatformStorage* storage, PlatformAudio* audio,
         PlatformIdentity* identity, PlatformOta* ota)
    : store_(store),
      clock_(clock),
      wifi_(wifi),
      cloud_(cloud),
      display_(display),
      storage_(storage),
      audio_(audio),
      identity_(identity),
      ota_(ota) {}

void App::set_build_id(std::string_view id) {
  pending_build_id_.assign(id.begin(), id.end());
}

void App::boot() {
  cfg_ = store_.load();
  if (!cfg_.fw_build_id.empty()) {
    // Keep flashed identity over any stale NVS version string.
  } else if (!cfg_.fw_version.empty() && cfg_.fw_version.rfind("POCKET-LIVE", 0) == 0) {
    cfg_.fw_build_id = cfg_.fw_version;
  }
  // Prefer identity stamped by app_main (kBuildId) when set before boot.
  if (!pending_build_id_.empty()) {
    cfg_.fw_build_id = pending_build_id_;
  }
  // Consumer-facing version is always the product semver — never a flash marker.
  cfg_.fw_version = kConsumerVersion;
  if (cfg_.device_name.empty()) cfg_.device_name = kProductName;
  if (cfg_.device_id.empty()) {
    if (identity_) cfg_.device_id = identity_->device_uuid();
    if (cfg_.device_id.empty()) cfg_.device_id = "00000000-0000-4000-8000-000000000001";
    store_.save(cfg_);
  }
  now_ms_ = clock_.now_ms();
  last_input_ms_ = now_ms_;
  if (audio_) audio_->set_volume(cfg_.volume_percent);
  display_.set_brightness(cfg_.brightness_percent);
  if (!cfg_.onboarding_complete) {
    const ScreenId resume = resume_onboarding_screen();
    if (resume == ScreenId::OnboardingWifiPassword || resume == ScreenId::OnboardingCompanionQr) {
      // SoftAP / pair need live sessions — restart Link without Welcome → Download → SD.
      begin_softap_link();
      return;
    }
    nav_.reset(resume);
  } else {
    nav_.reset(ScreenId::Lock);
    // Card already seated at boot is not a hot-insert — only interrupt on new insert.
    if (storage_) {
      storage_->probe();
      sd_was_present_ = storage_->present();
    }
    // Reconnect to a known network after power-cycle (password now persisted).
    wifi_boot_reconnect_done_ = false;
    last_wifi_reconnect_ms_ = 0;
  }
  after_nav();
}

static bool pin_hash_set(const DeviceConfig& cfg) {
  for (uint8_t b : cfg.pin_hash) {
    if (b != 0) return true;
  }
  return false;
}

ScreenId App::resume_onboarding_screen() const {
  // Prefer farthest durable progress so reboot does not restart Welcome.
  if (pin_hash_set(cfg_)) {
    return ScreenId::OnboardingTimezone;
  }
  if (cfg_.companion_linked) {
    return ScreenId::OnboardingPinLength;
  }
  if (!cfg_.wifi_ssid.empty() || !cfg_.wifi_known.empty()) {
    // SoftAP / Link again so phone can confirm or send another network.
    return ScreenId::OnboardingWifiPassword;
  }
  return ScreenId::OnboardingWelcome;
}

void App::ensure_softap_credentials_shown() {
  if (!wifi_.provisioning()) {
    std::string ap;
    std::string pass;
    if (wifi_.start_provision(cfg_.wifi_ssid, &ap, &pass)) {
      wifi_ap_ssid_ = ap.empty() ? wifi_.provision_ap_ssid() : ap;
      wifi_ap_pass_ = pass.empty() ? wifi_.provision_ap_password() : pass;
    }
  } else {
    wifi_ap_ssid_ = wifi_.provision_ap_ssid();
    wifi_ap_pass_ = wifi_.provision_ap_password();
  }
}

void App::play_sound(SoundId id) {
  if (audio_) audio_->play(id);
}

void App::mark_content_dirty() {
  // Only refuse to downgrade when a full refresh is already pending.
  if (dirty_ && dirty_kind_ == DirtyKind::FullCanvas) return;
  dirty_ = true;
  dirty_kind_ = DirtyKind::ContentBand;
}

void App::mark_status_dirty() {
  if (dirty_ && (dirty_kind_ == DirtyKind::FullCanvas || dirty_kind_ == DirtyKind::ContentBand ||
                 dirty_kind_ == DirtyKind::Region))
    return;
  dirty_ = true;
  dirty_kind_ = DirtyKind::StatusBar;
}

void App::mark_region_dirty(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  // Don't shrink an already-pending full or content-band update.
  if (dirty_ && (dirty_kind_ == DirtyKind::FullCanvas || dirty_kind_ == DirtyKind::ContentBand)) return;
  if (dirty_ && dirty_kind_ == DirtyKind::Region) {
    const int x1 = std::min(dirty_rx_, x);
    const int y1 = std::min(dirty_ry_, y);
    const int x2 = std::max(dirty_rx_ + dirty_rw_, x + w);
    const int y2 = std::max(dirty_ry_ + dirty_rh_, y + h);
    dirty_rx_ = x1;
    dirty_ry_ = y1;
    dirty_rw_ = x2 - x1;
    dirty_rh_ = y2 - y1;
    return;
  }
  dirty_ = true;
  dirty_kind_ = DirtyKind::Region;
  dirty_rx_ = x;
  dirty_ry_ = y;
  dirty_rw_ = w;
  dirty_rh_ = h;
}

void App::pin_band_geometry(int& x, int& y, int& w, int& h) const {
  // Covers progress label, digit slots, hint, error, and lockout strip.
  x = 0;
  y = 140;
  w = kCanvasW;
  h = 360;
}

void App::mark_pin_dirty() {
  int x = 0, y = 0, w = 0, h = 0;
  pin_band_geometry(x, y, w, h);
  mark_region_dirty(x, y, w, h);
}

void App::begin_softap_link() {
  if (wifi_.connected()) {
    // Already on home Wi‑Fi — mint pair code; skip SoftAP.
    if (mint_pair_session()) {
      play_sound(SoundId::Success);
      focus_.index = 0;
      nav_.replace(ScreenId::OnboardingCompanionQr);
      after_nav();
      return;
    }
    play_sound(SoundId::Attention);
    // Fall through to SoftAP if mint failed (still need a path forward).
  }
  std::string ap;
  std::string pass;
  if (!wifi_.start_provision(cfg_.wifi_ssid, &ap, &pass)) {
    error_msg_ = "Couldn't start phone setup.";
    error_until_ms_ = now_ms_ + 3000;
    mark_content_dirty();
    return;
  }
  wifi_ap_ssid_ = ap.empty() ? wifi_.provision_ap_ssid() : ap;
  wifi_ap_pass_ = pass.empty() ? wifi_.provision_ap_password() : pass;
  wifi_sta_connecting_ = false;
  last_wifi_prov_poll_ms_ = 0;
  focus_.index = 0;
  // Stay on SoftAP screen if already there — avoid full remount bounce.
  if (nav_.current() == ScreenId::OnboardingWifiPassword) {
    mark_content_dirty();
    return;
  }
  nav_.replace(ScreenId::OnboardingWifiPassword);
  after_nav();
}

bool App::connect_and_remember(const std::string& ssid, const std::string& password) {
  if (ssid.empty()) return false;
  wifi_password_ = password;
  const bool ok = wifi_.connect(ssid, password);
  if (ok) {
    wifi_known_upsert(cfg_, ssid, password);
    store_.save(cfg_);
    wifi_boot_reconnect_done_ = true;
    last_wifi_reconnect_ms_ = now_ms_;
  }
  return ok;
}

void App::begin_add_wifi_network() {
  // SoftAP while optionally staying on current STA (APSTA) so user can add another network.
  std::string ap;
  std::string pass;
  if (!wifi_.start_provision(cfg_.wifi_ssid, &ap, &pass)) {
    error_msg_ = "Couldn't start phone setup.";
    error_until_ms_ = now_ms_ + 3000;
    mark_content_dirty();
    return;
  }
  wifi_ap_ssid_ = ap.empty() ? wifi_.provision_ap_ssid() : ap;
  wifi_ap_pass_ = pass.empty() ? wifi_.provision_ap_password() : pass;
  wifi_sta_connecting_ = false;
  last_wifi_prov_poll_ms_ = 0;
  focus_.index = 0;
  nav_.replace(ScreenId::OnboardingWifiPassword);
  after_nav();
}

void App::maybe_wifi_auto_reconnect() {
  if (!cfg_.onboarding_complete) return;
  if (wifi_.connected() || wifi_.provisioning() || wifi_sta_connecting_) return;
  if (cfg_.wifi_known.empty()) return;
  // SoftAP / pair screens own Wi‑Fi.
  const ScreenId s = nav_.current();
  if (s == ScreenId::OnboardingWifiPassword || s == ScreenId::OnboardingWifiList ||
      s == ScreenId::OnboardingWifiConnecting) {
    return;
  }

  const uint32_t interval_ms = wifi_boot_reconnect_done_ ? 45000u : 800u;
  if (last_wifi_reconnect_ms_ != 0 && now_ms_ - last_wifi_reconnect_ms_ < interval_ms) return;
  last_wifi_reconnect_ms_ = now_ms_;
  wifi_sta_connecting_ = true;

  // Prefer last SSID, then other known entries. Try scan match first when possible.
  std::vector<WifiKnownNetwork> order = cfg_.wifi_known;
  if (!cfg_.wifi_ssid.empty()) {
    for (size_t i = 0; i < order.size(); ++i) {
      if (order[i].ssid == cfg_.wifi_ssid) {
        if (i != 0) std::swap(order[0], order[i]);
        break;
      }
    }
  }

  std::vector<std::string> visible;
  // Boot: try preferred blindly first (faster); later retries may scan.
  if (wifi_boot_reconnect_done_) {
    visible = wifi_.scan();
  }

  bool ok = false;
  for (const auto& net : order) {
    if (!visible.empty()) {
      bool seen = false;
      for (const auto& v : visible) {
        if (v == net.ssid) {
          seen = true;
          break;
        }
      }
      if (!seen) continue;
    }
    if (wifi_.connect(net.ssid, net.password)) {
      cfg_.wifi_ssid = net.ssid;
      wifi_password_ = net.password;
      store_.save(cfg_);
      ok = true;
      break;
    }
  }
  // If scan filtered everything out, try preferred blindly once.
  if (!ok && !order.empty() && !visible.empty()) {
    const auto& net = order.front();
    if (wifi_.connect(net.ssid, net.password)) {
      cfg_.wifi_ssid = net.ssid;
      wifi_password_ = net.password;
      store_.save(cfg_);
      ok = true;
    }
  }

  wifi_sta_connecting_ = false;
  wifi_boot_reconnect_done_ = true;
  if (ok) mark_status_dirty();
}

bool App::mint_pair_session() {
  // Retry a few times — Cloud must register the code or claim will fail.
  for (int attempt = 0; attempt < 3; ++attempt) {
    const std::string code = cloud_.create_pair_session(cfg_.device_id);
    if (!code.empty()) {
      pair_code_ = code;
      pair_expires_ms_ = now_ms_ + 10 * 60 * 1000;
      pair_status_ = "pending";
      last_pair_poll_ms_ = 0;
      return true;
    }
  }
  pair_code_.clear();
  pair_status_ = "expired";
  error_msg_ = "Couldn't create pairing code. Try again.";
  error_until_ms_ = now_ms_ + 4000;
  return false;
}

void App::after_nav(bool full_refresh) {
  focus_.index = 0;
  pin_digit_working_ = '0';
  dirty_ = true;
  dirty_kind_ = DirtyKind::FullCanvas;
  if (full_refresh) refresh_.on_screen_enter_full();
  if (nav_.current() == ScreenId::OnboardingWelcome && !welcome_sound_played_) {
    welcome_sound_played_ = true;
    play_sound(SoundId::Welcome);
  }
  redraw(full_refresh);
}

void App::after_nav() { after_nav(screen_requires_full_enter(nav_.current())); }

void App::go_home() {
  nav_.reset(ScreenId::Home);
  after_nav();
}

void App::go_lock() {
  nav_.reset(ScreenId::Lock);
  pin_entry_.clear();
  parental_session_unlocked_ = false;
  parental_pin_for_app_ = false;
  lock_motif_index_ = (lock_motif_index_ + 1) % kLockMotifCount;
  after_nav();
}

bool App::parental_requires_pin(HomeApp app) const {
  if (!home_app_is_real(app)) return false;
  if (app == HomeApp::Settings || app == HomeApp::Update) return false;
  return (cfg_.parental_pin_gated & (1u << static_cast<uint8_t>(app))) != 0;
}

void App::launch_home_app(HomeApp launch) {
  switch (launch) {
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
    case HomeApp::Reading:
      book_index_ = 0;
      nav_.push(ScreenId::ReadingList);
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
}

void App::maybe_cloud_attest() {
  if (!cfg_.onboarding_complete || !cfg_.companion_linked) return;
  if (!wifi_.connected() || cfg_.device_id.empty()) return;
  // ~20s so Companion “Online” / rename stay fresh after reconnect.
  if (last_attest_ms_ != 0 && now_ms_ - last_attest_ms_ < 20000u) return;
  last_attest_ms_ = now_ms_;
  const std::string body = cloud_.device_attest_json(cfg_.device_id);
  if (body.empty()) return;

  auto json_bool = [](const std::string& j, const char* key) -> int {
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = j.find(needle);
    if (p == std::string::npos) return -1;
    p = j.find(':', p + needle.size());
    if (p == std::string::npos) return -1;
    while (p + 1 < j.size() && (j[p + 1] == ' ' || j[p + 1] == '\t')) ++p;
    if (j.compare(p + 1, 4, "true") == 0) return 1;
    if (j.compare(p + 1, 5, "false") == 0) return 0;
    return -1;
  };
  auto json_str = [](const std::string& j, const char* key) -> std::string {
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = j.find(needle);
    if (p == std::string::npos) return {};
    p = j.find(':', p + needle.size());
    if (p == std::string::npos) return {};
    p = j.find('"', p + 1);
    if (p == std::string::npos) return {};
    size_t end = j.find('"', p + 1);
    if (end == std::string::npos) return {};
    return j.substr(p + 1, end - p - 1);
  };

  const int entitled = json_bool(body, "cloud_entitled");
  if (entitled >= 0) cfg_.cloud_entitled = entitled == 1;
  const std::string st = json_str(body, "status");
  if (!st.empty()) cfg_.cloud_status = st;
  const std::string dname = json_str(body, "device_name");
  if (!dname.empty() && dname != cfg_.device_name) {
    cfg_.device_name = dname;
  }
  // Optional lock/sleep face message — Companion is the source of truth once linked.
  const size_t lm_key = body.find("\"lock_message\"");
  if (lm_key != std::string::npos) {
    const std::string lmsg = json_str(body, "lock_message");
    if (lmsg != cfg_.lock_message) cfg_.lock_message = lmsg;
  }
  // Volume / brightness — either side can change these (rotary on-device, Companion remotely);
  // whichever changed most recently wins on the next pull since the pusher writes through too.
  auto json_int = [](const std::string& j, const char* key) -> int {
    const std::string needle = std::string("\"") + key + "\"";
    size_t p = j.find(needle);
    if (p == std::string::npos) return -1;
    p = j.find(':', p + needle.size());
    if (p == std::string::npos) return -1;
    ++p;
    while (p < j.size() && (j[p] == ' ' || j[p] == '\t')) ++p;
    return std::atoi(j.c_str() + p);
  };
  const int remote_vol = json_int(body, "volume_percent");
  if (remote_vol >= 0 && remote_vol <= 100 && remote_vol != static_cast<int>(cfg_.volume_percent)) {
    cfg_.volume_percent = static_cast<uint8_t>(remote_vol);
    if (audio_) audio_->set_volume(cfg_.volume_percent);
  }
  const int remote_bright = json_int(body, "brightness_percent");
  if (remote_bright >= 0 && remote_bright <= 100 && remote_bright != static_cast<int>(cfg_.brightness_percent)) {
    cfg_.brightness_percent = static_cast<uint8_t>(remote_bright);
    display_.set_brightness(cfg_.brightness_percent);
  }

  // Parental pin_gated_apps: ["notes","music",...]
  uint16_t gated = 0;
  const size_t arr = body.find("\"pin_gated_apps\"");
  if (arr != std::string::npos) {
    size_t lb = body.find('[', arr);
    size_t rb = body.find(']', lb == std::string::npos ? arr : lb);
    if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
      const std::string list = body.substr(lb, rb - lb + 1);
      auto has = [&](const char* name) { return list.find(name) != std::string::npos; };
      if (has("notes")) gated |= 1u << static_cast<uint8_t>(HomeApp::Notes);
      if (has("ledger")) gated |= 1u << static_cast<uint8_t>(HomeApp::Ledger);
      if (has("clock")) gated |= 1u << static_cast<uint8_t>(HomeApp::Clock);
      if (has("pass")) gated |= 1u << static_cast<uint8_t>(HomeApp::Pass);
      if (has("weather")) gated |= 1u << static_cast<uint8_t>(HomeApp::Weather);
      if (has("music")) gated |= 1u << static_cast<uint8_t>(HomeApp::Music);
    }
  }
  cfg_.parental_pin_gated = gated;
  const int hide_pass = json_bool(body, "hide_pass_share");
  if (hide_pass >= 0) cfg_.parental_hide_pass_share = hide_pass == 1;
  const int block_conn = json_bool(body, "block_connectors");
  if (block_conn >= 0) cfg_.parental_block_connectors = block_conn == 1;
  const int force_lock = json_bool(body, "force_lock");
  if (force_lock == 1 && nav_.current() != ScreenId::Lock && nav_.current() != ScreenId::Pin) {
    go_lock();
  }

  // Pending Wi‑Fi from Companion (in-app, no SoftAP hop).
  const std::string pending_ssid = json_str(body, "ssid");
  // pending_wifi object nests ssid — look near pending_wifi key
  std::string wifi_ssid;
  std::string wifi_pass;
  const size_t pw = body.find("\"pending_wifi\"");
  if (pw != std::string::npos && body.find("null", pw) != pw + 16) {
    const size_t obj = body.find('{', pw);
    const size_t end = obj == std::string::npos ? std::string::npos : body.find('}', obj);
    if (obj != std::string::npos && end != std::string::npos) {
      const std::string chunk = body.substr(obj, end - obj + 1);
      wifi_ssid = json_str(chunk, "ssid");
      wifi_pass = json_str(chunk, "password");
    }
  }
  (void)pending_ssid;
  if (!wifi_ssid.empty()) {
    connect_and_remember(wifi_ssid, wifi_pass);
  }

  store_.save(cfg_);
  mark_status_dirty();
}

void App::notify_wifi_connected() {
  last_attest_ms_ = 0;
  maybe_cloud_attest();
  mark_status_dirty();
}

void App::tick(uint32_t now_ms) {
  now_ms_ = now_ms;
  if (cfg_.onboarding_complete && nav_.current() != ScreenId::Lock && nav_.current() != ScreenId::Pin) {
    if (now_ms - last_input_ms_ >= static_cast<uint32_t>(cfg_.idle_lock_s) * 1000u) {
      go_lock();
      return;
    }
  }
  // SoftAP Wi‑Fi provision: wait for phone to POST credentials
  if (nav_.current() == ScreenId::OnboardingWifiPassword && !wifi_sta_connecting_) {
    if (now_ms - last_wifi_prov_poll_ms_ >= 400) {
      last_wifi_prov_poll_ms_ = now_ms;
      std::string ssid;
      std::string pass;
      if (wifi_.take_provision_credentials(&ssid, &pass)) {
        wifi_sta_connecting_ = true;
        error_msg_.clear();
        mark_content_dirty();
        // Stay on SoftAP screen — SoftAP stays up (APSTA) during STA attempt.
        const bool ok = connect_and_remember(ssid, pass);
        wifi_sta_connecting_ = false;
        if (ok) {
          if (cfg_.onboarding_complete) {
            focus_.index = 0;
            nav_.replace(ScreenId::SettingsWifi);
            after_nav();
          } else if (mint_pair_session()) {
            play_sound(SoundId::Success);
            focus_.index = 0;
            nav_.replace(ScreenId::OnboardingCompanionQr);
            after_nav();  // QR: full
          } else {
            play_sound(SoundId::Attention);
            ensure_softap_credentials_shown();
            mark_content_dirty();
          }
        } else {
          error_msg_ = "Couldn't connect. Check the password on your phone.";
          error_until_ms_ = now_ms_ + 4000;
          ensure_softap_credentials_shown();
          mark_content_dirty();
        }
      }
    }
  }
  // Pairing poll (throttle HTTP — UI loop is ~50ms)
  if (nav_.current() == ScreenId::OnboardingCompanionQr && !pair_code_.empty()) {
    if (now_ms > pair_expires_ms_) {
      if (pair_status_ != "expired") {
        pair_status_ = "expired";
        mark_content_dirty();
      }
    } else if (now_ms - last_pair_poll_ms_ >= 2500) {
      last_pair_poll_ms_ = now_ms;
      auto st = cloud_.pair_status(pair_code_);
      if (st == "claimed") {
        pair_status_ = "claimed";
        cfg_.companion_linked = true;
        store_.save(cfg_);
        play_sound(SoundId::Success);
        // Companion required: advance as soon as linked (no Skip path).
        if (!cfg_.onboarding_complete) {
          focus_.index = 0;
          nav_.replace(ScreenId::OnboardingPinLength);
          after_nav();
        } else {
          mark_content_dirty();
        }
      } else if (st == "expired" && pair_status_ != "expired") {
        pair_status_ = "expired";
        mark_content_dirty();
      }
    }
  }
  // SD eject wait (setup SoftAP path or anytime gate)
  if ((nav_.current() == ScreenId::OnboardingSdCard || nav_.current() == ScreenId::SdCardGate) &&
      sd_waiting_eject_) {
    if (now_ms - last_sd_poll_ms_ >= 500) {
      last_sd_poll_ms_ = now_ms;
      const bool still = storage_ && storage_->present();
      if (!still) {
        sd_waiting_eject_ = false;
        play_sound(SoundId::Click);
        if (nav_.current() == ScreenId::SdCardGate) {
          sd_was_present_ = false;
          leave_sd_gate();
        } else {
          begin_softap_link();
        }
      }
    }
  }
  if (ptt_active_ && mic_capturing_ && audio_) audio_->poll_capture();
  maybe_poll_sd_hotplug();
  maybe_wifi_auto_reconnect();
  maybe_cloud_attest();
  if (dirty_) {
    present_canvas(false);
    dirty_ = false;
  }
  maybe_tick_status_chrome();
  maybe_tick_clock_face();
  maybe_tick_home_clock();
}

void App::present_canvas(bool full) {
  canvas_.clear(Gray::G3);
  render();
  if (full || dirty_kind_ == DirtyKind::FullCanvas) {
    RefreshMode mode = refresh_.plan(!full);
    if (full) mode = RefreshMode::Full;
    display_.present(canvas_, mode);
    refresh_.on_applied(mode);
    dirty_kind_ = DirtyKind::FullCanvas;
    return;
  }
  // Region / status / content — honor ghosting budget (N partials → full).
  RefreshMode mode = refresh_.plan(true);
  if (mode == RefreshMode::Full) {
    display_.present(canvas_, RefreshMode::Full);
    refresh_.on_applied(mode);
    dirty_kind_ = DirtyKind::FullCanvas;
    return;
  }
  if (dirty_kind_ == DirtyKind::StatusBar) {
    display_.present_region(canvas_, 0, 0, kCanvasW, kStatusBarH);
    refresh_.on_applied_tiny(RefreshMode::Partial);
  } else if (dirty_kind_ == DirtyKind::Region) {
    display_.present_region(canvas_, dirty_rx_, dirty_ry_, dirty_rw_, dirty_rh_);
    // PIN / home tile regions are small — discount toward ghosting.
    if (dirty_rh_ <= kStatusBarH + 40 || dirty_rw_ * dirty_rh_ < (kCanvasW * kCanvasH) / 4) {
      refresh_.on_applied_tiny(RefreshMode::Partial);
    } else {
      refresh_.on_applied(RefreshMode::Partial);
    }
  } else {
    // Content below status bar — true region partial when one UI piece changes.
    display_.present_region(canvas_, 0, kStatusBarH, kCanvasW, kCanvasH - kStatusBarH);
    refresh_.on_applied(RefreshMode::Partial);
  }
  dirty_kind_ = DirtyKind::FullCanvas;
}

void App::redraw(bool full) {
  if (full) dirty_kind_ = DirtyKind::FullCanvas;
  present_canvas(full);
  dirty_ = false;
}

void App::draw_status_bar() {
  int h = 0, m = 0, wd = 0, mo = 0, d = 0;
  clock_.local_hm(h, m, wd, mo, d);
  const bool wifi_ok = wifi_.connected() || wifi_.provisioning();
  draw_status_bar_impl(canvas_, cfg_, h, m, wifi_ok, clock_.battery_percent(), clock_.time_valid());
}

void App::handle(InputEvent e) {
  if (e == InputEvent::None) return;
  last_input_ms_ = now_ms_;

  if (e == InputEvent::Home) {
    if (!long_home_blocked(nav_.current(), cfg_.onboarding_complete)) {
      go_home();
    }
    return;
  }
  if (e == InputEvent::Power) {
    // Setup incomplete: stay on onboarding — do not jump to Lock.
    if (!cfg_.onboarding_complete) return;
    go_lock();
    return;
  }

  switch (nav_.current()) {
    case ScreenId::Lock:
      handle_lock(e);
      break;
    case ScreenId::Pin:
      handle_pin(e);
      break;
    case ScreenId::OnboardingWelcome:
    case ScreenId::OnboardingCompanionDownload:
    case ScreenId::OnboardingSdCard:
    case ScreenId::OnboardingWifiList:
    case ScreenId::OnboardingWifiPassword:
    case ScreenId::OnboardingWifiConnecting:
    case ScreenId::OnboardingCompanionQr:
    case ScreenId::OnboardingPinLength:
    case ScreenId::OnboardingPinSet:
    case ScreenId::OnboardingPinConfirm:
    case ScreenId::OnboardingTimezone:
    case ScreenId::OnboardingMicTest:
    case ScreenId::OnboardingDone:
      handle_onboarding(e);
      break;
    case ScreenId::SdCardGate:
      handle_sd_gate(e);
      break;
    case ScreenId::Home:
      handle_home(e);
      break;
    case ScreenId::NotesList:
    case ScreenId::NotesDetail:
    case ScreenId::ListsList:
    case ScreenId::ListsDetail:
      handle_notes(e);
      break;
    case ScreenId::LedgerComingSoon:
      handle_ledger(e);
      break;
    case ScreenId::ClockFace:
    case ScreenId::ClockAlarms:
    case ScreenId::ClockAlarmEdit:
    case ScreenId::ClockTimers:
    case ScreenId::ClockTimerRun:
      handle_clock(e);
      break;
    case ScreenId::PassList:
    case ScreenId::PassDetail:
      handle_pass(e);
      break;
    case ScreenId::WeatherMain:
    case ScreenId::WeatherCitySetup:
      handle_weather(e);
      break;
    case ScreenId::MusicList:
    case ScreenId::MusicNowPlaying:
      handle_music(e);
      break;
    case ScreenId::ReadingList:
    case ScreenId::ReadingBook:
      handle_reading(e);
      break;
    default:
      handle_settings(e);
      break;
  }
  if (dirty_) {
    // Preserve dirty_kind_ (Region / StatusBar) — do not force FullCanvas.
    present_canvas(false);
    dirty_ = false;
  }
}

void App::render() {
  switch (nav_.current()) {
    case ScreenId::Lock:
      render_lock();
      break;
    case ScreenId::Pin:
      render_pin();
      break;
    case ScreenId::OnboardingWelcome:
    case ScreenId::OnboardingCompanionDownload:
    case ScreenId::OnboardingSdCard:
    case ScreenId::OnboardingWifiList:
    case ScreenId::OnboardingWifiPassword:
    case ScreenId::OnboardingWifiConnecting:
    case ScreenId::OnboardingCompanionQr:
    case ScreenId::OnboardingPinLength:
    case ScreenId::OnboardingPinSet:
    case ScreenId::OnboardingPinConfirm:
    case ScreenId::OnboardingTimezone:
    case ScreenId::OnboardingMicTest:
    case ScreenId::OnboardingDone:
      render_onboarding();
      break;
    case ScreenId::SdCardGate:
      render_sd_gate();
      break;
    case ScreenId::Home:
      render_home();
      break;
    case ScreenId::NotesList:
    case ScreenId::NotesDetail:
    case ScreenId::ListsList:
    case ScreenId::ListsDetail:
      render_notes();
      break;
    case ScreenId::LedgerComingSoon:
      render_ledger();
      break;
    case ScreenId::ClockFace:
    case ScreenId::ClockAlarms:
    case ScreenId::ClockAlarmEdit:
    case ScreenId::ClockTimers:
    case ScreenId::ClockTimerRun:
      render_clock();
      break;
    case ScreenId::PassList:
    case ScreenId::PassDetail:
      render_pass();
      break;
    case ScreenId::WeatherMain:
    case ScreenId::WeatherCitySetup:
      render_weather();
      break;
    case ScreenId::MusicList:
    case ScreenId::MusicNowPlaying:
      render_music();
      break;
    case ScreenId::ReadingList:
    case ScreenId::ReadingBook:
      render_reading();
      break;
    default:
      render_settings();
      break;
  }
}

// --- Lock / PIN -----------------------------------------------------------

void App::render_lock() {
  // Calm face — no clock, no press hint, no battery %. Static art needs no ticking refresh.
  const int wm_w = canvas_.pocket_wordmark_width();
  const int wm_cx = kCanvasW / 2 + 18;  // nudge right so the mark balances the wordmark
  draw_pocket_mark(wm_cx - wm_w / 2 - 14, 56 + canvas_.pocket_wordmark_height() / 2);
  canvas_.draw_pocket_wordmark(wm_cx - wm_w / 2, 56, Gray::G0);

  draw_lock_motif();

  // Optional custom message — set in Settings → Display or Companion. Empty by default.
  if (!cfg_.lock_message.empty()) {
    canvas_.draw_text_fit(kSideMargin, 762, kContentW, cfg_.lock_message, Canvas::TextRole::Secondary,
                          Gray::G1);
  }
}

/** Small geometric mark to the left of the "Pocket" wordmark: a folded-corner square,
 * echoing a pocket without being a literal icon. `right_x` is the mark's right edge. */
void App::draw_pocket_mark(int right_x, int cy) {
  constexpr int kSize = 30;
  const int x = right_x - kSize;
  const int y = cy - kSize / 2;
  canvas_.stroke_round_rect(x, y, kSize, kSize, 6, Gray::G0, 2);
  // Folded corner — small filled triangle at top-right, reads as a pocket flap.
  constexpr int kFold = 11;
  for (int i = 0; i < kFold; ++i) {
    canvas_.hline(x + kSize - kFold + i, y + i, kFold - i, Gray::G0);
  }
}

void App::draw_lock_motif() {
  constexpr int kTop = 180;
  constexpr int kBot = 620;
  constexpr int cx = kCanvasW / 2;
  const int motif = ((lock_motif_index_ % kLockMotifCount) + kLockMotifCount) % kLockMotifCount;

  // Each motif below is a fixed composition with a name and a story — not a
  // randomized abstract pattern. `stroke_circle` is a tiny local helper: circles are
  // drawn as a per-row left/right edge pair (cheap, crisp on 2bpp e-ink, no font).
  auto stroke_circle = [this](int ox, int oy, int r, Gray g) {
    for (int yy = -r; yy <= r; ++yy) {
      const int half_w = static_cast<int>(std::sqrt(static_cast<double>(r * r - yy * yy)));
      canvas_.set_pixel(ox - half_w, oy + yy, g);
      canvas_.set_pixel(ox + half_w, oy + yy, g);
    }
  };

  switch (motif) {
    case 0: {
      // "Horizon" — a still line where sky meets ground, one quiet marker above it.
      const int horizon_y = kTop + 260;
      canvas_.hline(48, horizon_y, kCanvasW - 96, Gray::G0);
      stroke_circle(cx, horizon_y - 64, 46, Gray::G0);
      // Reflection — a calm echo of the line below, shorter as it recedes.
      for (int i = 0; i < 4; ++i) {
        const int y = horizon_y + 26 + i * 30;
        const int inset = 90 + i * 34;
        canvas_.hline(inset, y, kCanvasW - 2 * inset, (i % 2 == 0) ? Gray::G1 : Gray::G2);
      }
      break;
    }
    case 1: {
      // "Tide rings" — ripples settling outward from one still point in the water.
      const int oy = (kTop + kBot) / 2;
      constexpr int kRadii[] = {30, 68, 108, 150, 194};
      for (size_t i = 0; i < 5; ++i) {
        const Gray g = (i % 2 == 0) ? Gray::G1 : Gray::G2;
        stroke_circle(cx, oy, kRadii[i], g);
      }
      canvas_.fill_round_rect(cx - 6, oy - 6, 12, 12, 6, Gray::G0);
      break;
    }
    case 2: {
      // "Folded paper plane" — a still, angular fuselage with one raised wing fold.
      struct Pt { int x, y; };
      const Pt nose{cx + 90, kTop + 60};
      const Pt tail{cx - 110, kBot - 60};
      const Pt wing_l{cx - 170, kBot - 130};
      const Pt wing_r{cx + 140, kBot - 190};
      const Pt fold{cx - 10, kBot - 110};
      canvas_.line(nose.x, nose.y, tail.x, tail.y, Gray::G0);      // spine
      canvas_.line(nose.x, nose.y, wing_l.x, wing_l.y, Gray::G1);  // left edge
      canvas_.line(nose.x, nose.y, wing_r.x, wing_r.y, Gray::G1);  // right edge
      canvas_.line(wing_l.x, wing_l.y, fold.x, fold.y, Gray::G2);  // left wing fold
      canvas_.line(wing_r.x, wing_r.y, fold.x, fold.y, Gray::G2);  // raised wing fold
      canvas_.line(fold.x, fold.y, tail.x, tail.y, Gray::G1);
      canvas_.fill_round_rect(nose.x - 5, nose.y - 5, 10, 10, 5, Gray::G0);
      break;
    }
    case 3: {
      // "Quiet constellation" — a scatter of stars joined by thin lines. No clock hands.
      struct Pt { int x, y; };
      const Pt pts[] = {
          {cx - 120, kTop + 60},  {cx + 40, kTop + 30},   {cx + 130, kTop + 140},
          {cx - 30, kTop + 190},  {cx - 150, kTop + 260}, {cx + 90, kTop + 280},
          {cx, kTop + 360},       {cx - 90, kTop + 420},  {cx + 150, kTop + 400},
      };
      constexpr int kEdges[][2] = {{0, 1}, {1, 2}, {1, 3}, {3, 4}, {3, 5}, {2, 5}, {5, 6}, {4, 7}, {6, 8}, {6, 7}};
      for (const auto& e : kEdges) {
        canvas_.line(pts[e[0]].x, pts[e[0]].y, pts[e[1]].x, pts[e[1]].y, Gray::G2);
      }
      for (const auto& p : pts) {
        canvas_.fill_round_rect(p.x - 5, p.y - 5, 10, 10, 5, Gray::G0);
      }
      break;
    }
    case 4: {
      // "City grid at dusk" — a quiet skyline resting on a still baseline.
      const int base_y = kBot - 20;
      canvas_.hline(40, base_y, kCanvasW - 80, Gray::G0);
      struct Bldg { int x, w, h; };
      const Bldg blds[] = {
          {56, 44, 210}, {108, 28, 150}, {144, 52, 270}, {204, 34, 180},
          {246, 58, 310}, {312, 32, 160}, {352, 48, 230}, {408, 28, 140},
      };
      for (const auto& b : blds) {
        const int y0 = base_y - b.h;
        canvas_.stroke_rect(b.x, y0, b.w, b.h, Gray::G1);
        for (int wy = y0 + 22; wy < base_y - 18; wy += 40) {
          canvas_.fill_round_rect(b.x + b.w / 2 - 4, wy, 8, 8, 2, Gray::G2);
        }
      }
      break;
    }
    default: {
      // "Aperture" — concentric frames closing evenly toward one still point.
      constexpr int kSteps = 6;
      const int max_size = kBot - kTop;
      for (int i = 0; i < kSteps; ++i) {
        const int size = max_size - i * 70;
        if (size <= 40) break;
        const int x = cx - size / 2;
        const int y = kTop + (max_size - size) / 2;
        const Gray g = (i % 2 == 0) ? Gray::G1 : Gray::G2;
        canvas_.stroke_round_rect(x, y, size, size, 24, g, 2);
      }
      canvas_.fill_round_rect(cx - 8, kTop + max_size / 2 - 8, 16, 16, 8, Gray::G0);
      break;
    }
  }
}

void App::handle_lock(InputEvent e) {
  if (e == InputEvent::Select) {
    pin_entry_.clear();
    nav_.push(ScreenId::Pin);
    after_nav();
  }
  // Up/Down/Back/Home ignored per Spec
}

void App::draw_pin_entry(bool mask_completed, int band_top) {
  const int n = cfg_.pin_length;
  constexpr int kSlotW = 52;
  constexpr int kSlotH = 72;
  constexpr int kGap = 14;
  const int total = n * kSlotW + (n - 1) * kGap;
  const int x0 = (kCanvasW - total) / 2;
  const int slot_y = band_top;

  // Progress — which digit we're on (preview slot = next to fill).
  char prog[32];
  const int at = std::min(static_cast<int>(pin_entry_.size()) + 1, n);
  std::snprintf(prog, sizeof(prog), "Digit %d of %d", at, n);
  canvas_.draw_text_centered(kCanvasW / 2, slot_y - 36, prog, Canvas::TextRole::Secondary, Gray::G1);

  for (int i = 0; i < n; ++i) {
    const int x = x0 + i * (kSlotW + kGap);
    const bool focused = static_cast<int>(pin_entry_.size()) == i;
    const bool filled = i < static_cast<int>(pin_entry_.size());

    canvas_.stroke_rect(x, slot_y, kSlotW, kSlotH, focused ? Gray::G0 : Gray::G2);
    if (focused) {
      canvas_.stroke_rect(x + 2, slot_y + 2, kSlotW - 4, kSlotH - 4, Gray::G0);
      char s[2] = {pin_digit_working_, 0};
      const int tw = canvas_.text_width(s, Canvas::TextRole::PinDigit);
      canvas_.draw_text(x + (kSlotW - tw) / 2, slot_y + 16, s, Canvas::TextRole::PinDigit, Gray::G0);
    } else if (filled) {
      if (mask_completed) {
        // Disk for a locked-in digit.
        const int cx = x + kSlotW / 2;
        const int cy = slot_y + kSlotH / 2;
        canvas_.fill_rect(cx - 7, cy - 7, 14, 14, Gray::G0);
        canvas_.fill_rect(cx - 5, cy - 9, 10, 2, Gray::G3);
        canvas_.fill_rect(cx - 5, cy + 7, 10, 2, Gray::G3);
      } else {
        char s[2] = {pin_entry_[i], 0};
        const int tw = canvas_.text_width(s, Canvas::TextRole::PinDigit);
        canvas_.draw_text(x + (kSlotW - tw) / 2, slot_y + 16, s, Canvas::TextRole::PinDigit, Gray::G0);
      }
    } else {
      canvas_.hline(x + 10, slot_y + kSlotH / 2, kSlotW - 20, Gray::G2);
    }
  }

  canvas_.draw_text_centered(kCanvasW / 2, slot_y + kSlotH + 28, "Turn to choose · Press to set",
                             Canvas::TextRole::Secondary, Gray::G1);
  canvas_.draw_text_centered(kCanvasW / 2, slot_y + kSlotH + 56, "Back erases a digit",
                             Canvas::TextRole::Secondary, Gray::G1);
}

void App::render_pin() {
  draw_status_bar();
  canvas_.draw_text_centered(kCanvasW / 2, kContentTop + 8, "Enter PIN", Canvas::TextRole::ScreenTitle,
                             Gray::G0);
  draw_pin_entry(/*mask_completed=*/true, 200);
  if (now_ms_ < error_until_ms_) {
    canvas_.draw_text_centered(kCanvasW / 2, 420, error_msg_.c_str(), Canvas::TextRole::Body, Gray::G0);
  }
  if (now_ms_ < pin_lockout_until_ms_) {
    canvas_.draw_text_centered(kCanvasW / 2, 448, "Try again in 30 seconds", Canvas::TextRole::Body, Gray::G0);
  }
}

void App::handle_pin(InputEvent e) {
  if (now_ms_ < pin_lockout_until_ms_) return;
  if (e == InputEvent::Back) {
    if (pin_entry_.empty()) {
      parental_pin_for_app_ = false;
      nav_.pop();
      after_nav();
    } else {
      pin_entry_.pop_back();
      pin_digit_working_ = '0';
      focus_.index = static_cast<int>(pin_entry_.size());
      mark_pin_dirty();
    }
    return;
  }

  // Preview only — Select commits the digit (avoids slot-jump overwrite).
  if (e == InputEvent::Up) {
    pin_digit_working_ = static_cast<char>('0' + ((pin_digit_working_ - '0' + 9) % 10));
    mark_pin_dirty();
    return;
  }
  if (e == InputEvent::Down) {
    pin_digit_working_ = static_cast<char>('0' + ((pin_digit_working_ - '0' + 1) % 10));
    mark_pin_dirty();
    return;
  }
  if (e == InputEvent::Select) {
    if (static_cast<int>(pin_entry_.size()) < cfg_.pin_length) {
      pin_entry_.push_back(pin_digit_working_);
    }
    if (static_cast<int>(pin_entry_.size()) >= cfg_.pin_length) {
      if (verify_pin(cfg_, pin_entry_)) {
        pin_fail_count_ = 0;
        pin_digit_working_ = '0';
        parental_session_unlocked_ = true;
        if (parental_pin_for_app_) {
          parental_pin_for_app_ = false;
          const HomeApp pending = parental_pending_app_;
          pin_entry_.clear();
          nav_.pop();
          launch_home_app(pending);
        } else {
          pin_entry_.clear();
          go_home();
        }
      } else {
        error_msg_ = "Incorrect PIN";
        error_until_ms_ = now_ms_ + 2000;
        ++pin_fail_count_;
        pin_entry_.clear();
        focus_.index = 0;
        pin_digit_working_ = '0';
        if (pin_fail_count_ >= 5) {
          pin_lockout_until_ms_ = now_ms_ + 30000;
          pin_fail_count_ = 0;
        }
        mark_pin_dirty();
      }
    } else {
      focus_.index = static_cast<int>(pin_entry_.size());
      // Keep working digit so consecutive same digits are one press each.
      mark_pin_dirty();
    }
  }
}


void App::draw_home_clock() {
  int h = 0, m = 0, wd = 0, mo = 0, d = 0;
  clock_.local_hm(h, m, wd, mo, d);
  char tbuf[16];
  if (cfg_.time_format == 24) {
    std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(tbuf, sizeof(tbuf), "%d:%02d", h12, m);
  }
  constexpr int kClockTop = kContentTop + 40;
  constexpr int kClockH = 176;
  canvas_.fill_rect(kSideMargin, kClockTop, kCanvasW - 32, kClockH, Gray::G3);
  canvas_.draw_text_centered(kCanvasW / 2, kClockTop + 4, tbuf, Canvas::TextRole::HugeClock, Gray::G0);
  char dbuf[48];
  std::snprintf(dbuf, sizeof(dbuf), "%s, %s %d", kWeekdays[wd % 7], kMonths[mo % 12], d);
  canvas_.draw_text_centered(kCanvasW / 2, kClockTop + 156, dbuf, Canvas::TextRole::Secondary, Gray::G1);
  last_home_clock_minute_ = h * 60 + m;
}

void App::present_home_clock_partial() {
  draw_home_clock();
  constexpr int kClockTop = kContentTop + 40;
  constexpr int kClockH = 176;
  display_.present_region(canvas_, kSideMargin, kClockTop, kCanvasW - 32, kClockH);
  refresh_.on_applied(RefreshMode::Partial);
}

bool App::screen_has_status_bar() const {
  const ScreenId s = nav_.current();
  if (s == ScreenId::Lock) return false;
  if (s == ScreenId::PassDetail) return false;
  return true;
}

void App::maybe_tick_status_chrome() {
  if (!cfg_.onboarding_complete && nav_.current() == ScreenId::OnboardingWelcome) {
    // Welcome is full-bleed brand; still has status bar in onboarding path.
  }
  if (!screen_has_status_bar()) {
    // Lock: static art + optional message only — nothing here ticks, so no partial refresh needed.
    return;
  }

  int h = 0, m = 0, wd = 0, mo = 0, d = 0;
  clock_.local_hm(h, m, wd, mo, d);
  const int minute_key = h * 60 + m;
  const int batt = clock_.battery_percent();
  const bool wifi_ok = wifi_.connected() || wifi_.provisioning();
  const bool time_ok = clock_.time_valid();

  if (last_status_minute_ < 0) {
    last_status_minute_ = minute_key;
    last_status_battery_ = batt;
    last_status_wifi_ = wifi_ok;
    last_status_time_ok_ = time_ok;
    return;
  }

  const bool changed = (minute_key != last_status_minute_) || (batt != last_status_battery_) ||
                       (wifi_ok != last_status_wifi_) || (time_ok != last_status_time_ok_);
  if (!changed) return;

  last_status_minute_ = minute_key;
  last_status_battery_ = batt;
  last_status_wifi_ = wifi_ok;
  last_status_time_ok_ = time_ok;
  mark_status_dirty();
  if (dirty_) {
    present_canvas(false);
    dirty_ = false;
  }
}

void App::maybe_tick_clock_face() {
  const ScreenId s = nav_.current();
  // render_clock draws the huge time on ClockFace, or when clock_tab_==0 on clock roots.
  const bool on_clock = s == ScreenId::ClockFace || s == ScreenId::ClockAlarms ||
                        s == ScreenId::ClockTimers || s == ScreenId::ClockAlarmEdit ||
                        s == ScreenId::ClockTimerRun;
  if (!on_clock) return;
  if (s != ScreenId::ClockFace && clock_tab_ != 0) return;

  int h = 0, m = 0, wd = 0, mo = 0, d = 0;
  clock_.local_hm(h, m, wd, mo, d);
  const int minute_key = h * 60 + m;
  if (last_home_clock_minute_ < 0) {
    last_home_clock_minute_ = minute_key;
    return;
  }
  if (minute_key == last_home_clock_minute_) return;
  last_home_clock_minute_ = minute_key;
  // Huge clock band (see render_clock y≈280).
  mark_region_dirty(0, 220, kCanvasW, 160);
  if (dirty_) {
    present_canvas(false);
    dirty_ = false;
  }
}

void App::maybe_tick_home_clock() {
  // Home no longer hosts a large clock — time updates live in the status bar only.
  (void)0;
}

}  // namespace pocket