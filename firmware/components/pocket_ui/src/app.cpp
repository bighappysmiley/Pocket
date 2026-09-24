#include "pocket/app.hpp"
#include <algorithm>
#include <cstdio>
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
         PlatformIdentity* identity)
    : store_(store),
      clock_(clock),
      wifi_(wifi),
      cloud_(cloud),
      display_(display),
      storage_(storage),
      audio_(audio),
      identity_(identity) {}

void App::boot() {
  cfg_ = store_.load();
  if (cfg_.device_name.empty()) cfg_.device_name = "Pocket";
  if (cfg_.device_id.empty()) {
    if (identity_) cfg_.device_id = identity_->device_uuid();
    if (cfg_.device_id.empty()) cfg_.device_id = "00000000-0000-4000-8000-000000000001";
    store_.save(cfg_);
  }
  now_ms_ = clock_.now_ms();
  last_input_ms_ = now_ms_;
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
  if (!cfg_.wifi_ssid.empty()) {
    // Password is not persisted — SoftAP Link again (or skip if already online).
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
  // Covers progress label, digit slots, hint, and error strip for unlock + setup.
  x = 0;
  y = 140;
  w = kCanvasW;
  h = 320;
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
  after_nav();
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
        cfg_.wifi_ssid = ssid;
        wifi_password_ = pass;
        wifi_sta_connecting_ = true;
        error_msg_.clear();
        mark_content_dirty();
        // Stay on SoftAP screen — SoftAP stays up (APSTA) during STA attempt.
        const bool ok = wifi_.connect(cfg_.wifi_ssid, wifi_password_);
        wifi_sta_connecting_ = false;
        if (ok) {
          store_.save(cfg_);
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
  // SD eject wait: poll until card gone, then continue to SoftAP Link
  if (nav_.current() == ScreenId::OnboardingSdCard && sd_waiting_eject_) {
    if (now_ms - last_sd_poll_ms_ >= 500) {
      last_sd_poll_ms_ = now_ms;
      const bool still = storage_ && storage_->present();
      if (!still) {
        sd_waiting_eject_ = false;
        play_sound(SoundId::Click);
        begin_softap_link();
      }
    }
  }
  if (dirty_) {
    present_canvas(false);
    dirty_ = false;
  }
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
  if (dirty_kind_ == DirtyKind::StatusBar) {
    display_.present_region(canvas_, 0, 0, kCanvasW, kStatusBarH);
    refresh_.on_applied(RefreshMode::Partial);
  } else if (dirty_kind_ == DirtyKind::Region) {
    display_.present_region(canvas_, dirty_rx_, dirty_ry_, dirty_rw_, dirty_rh_);
    refresh_.on_applied(RefreshMode::Partial);
  } else {
    // Content below status bar — true region partial when one UI piece changes.
    display_.present_region(canvas_, 0, kStatusBarH, kCanvasW, kCanvasH - kStatusBarH);
    refresh_.on_applied(RefreshMode::Partial);
  }
  dirty_kind_ = DirtyKind::FullCanvas;
}

void App::redraw(bool full) {
  dirty_kind_ = DirtyKind::FullCanvas;
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
    default:
      handle_settings(e);
      break;
  }
  if (dirty_) redraw(false);
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
    default:
      render_settings();
      break;
  }
}

// --- Lock / PIN -----------------------------------------------------------

void App::render_lock() {
  // Calm abstract face — no clock (static art needs no minute refresh).
  canvas_.draw_text_centered(kCanvasW / 2, 56, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
  draw_lock_motif();
  canvas_.draw_text_centered(kCanvasW / 2, 735, "Battery", Canvas::TextRole::Secondary, Gray::G1);
  canvas_.draw_text_centered(kCanvasW / 2, 770, "Press to unlock", Canvas::TextRole::Secondary, Gray::G1);
}

void App::draw_lock_motif() {
  // Minimal line field that resolves into a quiet pocket-fold silhouette.
  constexpr int kTop = 180;
  constexpr int kBot = 620;
  constexpr int cx = kCanvasW / 2;

  for (int i = 0; i < 9; ++i) {
    const int y = kTop + i * 48;
    const int inset = 36 + ((i * 17) % 5) * 22;
    const Gray g = (i % 3 == 0) ? Gray::G1 : Gray::G2;
    canvas_.hline(inset, y, kCanvasW - 2 * inset, g);
  }

  // Converging diagonals — abstract lines that meet as a fold.
  canvas_.line(48, kTop + 20, cx - 12, kBot - 40, Gray::G1);
  canvas_.line(kCanvasW - 48, kTop + 20, cx + 12, kBot - 40, Gray::G1);
  canvas_.line(72, kTop + 80, cx, kBot - 100, Gray::G2);
  canvas_.line(kCanvasW - 72, kTop + 80, cx, kBot - 100, Gray::G2);

  // Soft vertical seam + base — reads as a pocket edge without being literal.
  canvas_.vline(cx, kTop + 60, kBot - kTop - 120, Gray::G0);
  canvas_.hline(cx - 90, kBot - 48, 180, Gray::G0);
  canvas_.hline(cx - 60, kBot - 36, 120, Gray::G1);
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

  // Progress — which digit we're on.
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
      char dig = pin_digit_working_;
      if (filled) dig = pin_entry_[i];
      char s[2] = {dig, 0};
      const int tw = canvas_.text_width(s, Canvas::TextRole::PinDigit);
      canvas_.draw_text(x + (kSlotW - tw) / 2, slot_y + 16, s, Canvas::TextRole::PinDigit, Gray::G0);
    } else if (filled) {
      if (mask_completed) {
        // Dot for a locked-in digit.
        const int cx = x + kSlotW / 2;
        const int cy = slot_y + kSlotH / 2;
        canvas_.fill_rect(cx - 6, cy - 6, 12, 12, Gray::G0);
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
    canvas_.draw_text_centered(kCanvasW / 2, 460, "Try again in 30 seconds", Canvas::TextRole::Body, Gray::G0);
  }
}

void App::handle_pin(InputEvent e) {
  if (now_ms_ < pin_lockout_until_ms_) return;
  if (e == InputEvent::Back) {
    if (pin_entry_.empty()) {
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

  if (e == InputEvent::Up) {
    pin_digit_working_ = static_cast<char>('0' + ((pin_digit_working_ - '0' + 9) % 10));
    if (pin_entry_.size() == static_cast<size_t>(focus_.index)) {
      pin_entry_.push_back(pin_digit_working_);
    } else if (focus_.index < static_cast<int>(pin_entry_.size())) {
      pin_entry_[focus_.index] = pin_digit_working_;
    }
    mark_pin_dirty();
    return;
  }
  if (e == InputEvent::Down) {
    pin_digit_working_ = static_cast<char>('0' + ((pin_digit_working_ - '0' + 1) % 10));
    if (pin_entry_.size() == static_cast<size_t>(focus_.index)) {
      pin_entry_.push_back(pin_digit_working_);
    } else if (focus_.index < static_cast<int>(pin_entry_.size())) {
      pin_entry_[focus_.index] = pin_digit_working_;
    }
    mark_pin_dirty();
    return;
  }
  if (e == InputEvent::Select) {
    if (pin_entry_.size() == static_cast<size_t>(focus_.index)) {
      pin_entry_.push_back(pin_digit_working_);
    }
    if (static_cast<int>(pin_entry_.size()) >= cfg_.pin_length) {
      if (verify_pin(cfg_, pin_entry_)) {
        pin_fail_count_ = 0;
        pin_digit_working_ = '0';
        go_home();
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
      pin_digit_working_ = '0';
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

void App::maybe_tick_home_clock() {
  // Home no longer hosts a large clock — time updates live in the status bar only.
  (void)0;
}

}  // namespace pocket