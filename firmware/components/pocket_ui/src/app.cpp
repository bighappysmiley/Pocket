#include "pocket/app.hpp"
#include <algorithm>
#include <cstdio>
#include <ctime>

namespace pocket {

extern void draw_status_bar_impl(Canvas& c, const DeviceConfig& cfg, int hour, int minute, bool wifi_ok,
                                 int battery_pct);

static const char* kWeekdays[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static const char* kTimezones[] = {"America/New_York", "America/Chicago", "America/Denver",
                                   "America/Los_Angeles", "America/Phoenix", "UTC", "Europe/London"};

App::App(ConfigStore& store, PlatformClock& clock, PlatformWifi& wifi, PlatformCloud& cloud,
         PlatformDisplay& display)
    : store_(store), clock_(clock), wifi_(wifi), cloud_(cloud), display_(display) {}

void App::boot() {
  cfg_ = store_.load();
  if (cfg_.device_name.empty()) cfg_.device_name = "Pocket";
  if (cfg_.device_id.empty()) cfg_.device_id = "00000000-0000-4000-8000-000000000001";
  now_ms_ = clock_.now_ms();
  last_input_ms_ = now_ms_;
  if (!cfg_.onboarding_complete) {
    nav_.reset(ScreenId::OnboardingWelcome);
  } else {
    nav_.reset(ScreenId::Lock);
  }
  after_nav(true);
}

void App::after_nav(bool full_refresh) {
  focus_.index = 0;
  dirty_ = true;
  if (full_refresh) refresh_.on_screen_enter_full();
  redraw(full_refresh);
}

void App::go_home() {
  nav_.reset(ScreenId::Home);
  after_nav(true);
}

void App::go_lock() {
  nav_.reset(ScreenId::Lock);
  pin_entry_.clear();
  after_nav(true);
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
  if (nav_.current() == ScreenId::OnboardingWifiPassword) {
    if (now_ms - last_wifi_prov_poll_ms_ >= 400) {
      last_wifi_prov_poll_ms_ = now_ms;
      std::string ssid;
      std::string pass;
      if (wifi_.take_provision_credentials(&ssid, &pass)) {
        cfg_.wifi_ssid = ssid;
        wifi_password_ = pass;
        nav_.replace(ScreenId::OnboardingWifiConnecting);
        after_nav(true);
        const bool ok = wifi_.connect(cfg_.wifi_ssid, wifi_password_);
        if (ok) {
          store_.save(cfg_);
          if (cfg_.onboarding_complete) {
            focus_.index = 0;
            nav_.replace(ScreenId::SettingsWifi);
            after_nav(true);
          } else {
            pair_code_ = cloud_.create_pair_session(cfg_.device_id);
            pair_expires_ms_ = now_ms_ + 10 * 60 * 1000;
            pair_status_ = "pending";
            last_pair_poll_ms_ = 0;
            focus_.index = 0;
            nav_.replace(ScreenId::OnboardingCompanionQr);
            after_nav(true);
          }
        } else {
          error_msg_ = "Couldn't connect. Check the password on your phone.";
          error_until_ms_ = now_ms_ + 4000;
          std::string ap;
          wifi_.start_provision(cfg_.wifi_ssid, &ap);
          wifi_ap_ssid_ = ap;
          focus_.index = 0;
          nav_.replace(ScreenId::OnboardingWifiPassword);
          after_nav(true);
        }
      }
    }
  }
  // Pairing poll (throttle HTTP — UI loop is ~50ms)
  if (nav_.current() == ScreenId::OnboardingCompanionQr && !pair_code_.empty()) {
    if (now_ms > pair_expires_ms_) {
      if (pair_status_ != "expired") {
        pair_status_ = "expired";
        dirty_ = true;
      }
    } else if (now_ms - last_pair_poll_ms_ >= 2500) {
      last_pair_poll_ms_ = now_ms;
      auto st = cloud_.pair_status(pair_code_);
      if (st == "claimed") {
        pair_status_ = "claimed";
        cfg_.companion_linked = true;
        store_.save(cfg_);
        dirty_ = true;
      } else if (st == "expired" && pair_status_ != "expired") {
        pair_status_ = "expired";
        dirty_ = true;
      }
    }
  }
  if (dirty_) redraw(false);
}

void App::redraw(bool full) {
  canvas_.clear(Gray::G3);
  render();
  RefreshMode mode = refresh_.plan(!full);
  if (full) mode = RefreshMode::Full;
  display_.present(canvas_, mode);
  refresh_.on_applied(mode);
  dirty_ = false;
}

void App::draw_status_bar() {
  int h = 0, m = 0, wd = 0, mo = 0, d = 0;
  clock_.local_hm(h, m, wd, mo, d);
  draw_status_bar_impl(canvas_, cfg_, h, m, wifi_.connected(), 78);
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
    default:
      render_settings();
      break;
  }
}

// --- Lock / PIN -----------------------------------------------------------

void App::render_lock() {
  int h = 0, m = 0, wd = 0, mo = 0, d = 0;
  clock_.local_hm(h, m, wd, mo, d);
  canvas_.draw_text_centered(kCanvasW / 2, 56, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
  char tbuf[16];
  if (cfg_.time_format == 24) {
    std::snprintf(tbuf, sizeof(tbuf), "%02d:%02d", h, m);
  } else {
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    std::snprintf(tbuf, sizeof(tbuf), "%d:%02d", h12, m);
  }
  canvas_.draw_text_centered(kCanvasW / 2, 300, tbuf, Canvas::TextRole::HugeClock, Gray::G0);
  char dbuf[48];
  std::snprintf(dbuf, sizeof(dbuf), "%s, %s %d", kWeekdays[wd % 7], kMonths[mo % 12], d);
  canvas_.draw_text_centered(kCanvasW / 2, 380, dbuf, Canvas::TextRole::Body, Gray::G0);
  canvas_.draw_text_centered(kCanvasW / 2, 735, "Battery", Canvas::TextRole::Secondary, Gray::G1);
  canvas_.draw_text_centered(kCanvasW / 2, 770, "Press to unlock", Canvas::TextRole::Secondary, Gray::G1);
}

void App::handle_lock(InputEvent e) {
  if (e == InputEvent::Select) {
    pin_entry_.clear();
    nav_.push(ScreenId::Pin);
    after_nav(true);
  }
  // Up/Down/Back/Home ignored per Spec
}

void App::render_pin() {
  draw_status_bar();
  canvas_.draw_text_centered(kCanvasW / 2, 48, "Enter PIN", Canvas::TextRole::ScreenTitle, Gray::G0);
  const int n = cfg_.pin_length;
  const int slot_w = 48;
  const int total = n * slot_w + (n - 1) * 12;
  int x0 = (kCanvasW - total) / 2;
  for (int i = 0; i < n; ++i) {
    int x = x0 + i * (slot_w + 12);
    bool focused = static_cast<int>(pin_entry_.size()) == i;
    char dig = (i < static_cast<int>(pin_entry_.size())) ? pin_entry_[i] : '0';
    char s[2] = {dig, 0};
    if (focused) {
      canvas_.draw_focus_tile(x, 200, slot_w, 64, s, Canvas::TextRole::PinDigit);
    } else if (i < static_cast<int>(pin_entry_.size())) {
      canvas_.draw_text(x + 8, 212, s, Canvas::TextRole::PinDigit, Gray::G0);
    } else {
      canvas_.hline(x, 250, slot_w, Gray::G2);
    }
  }
  canvas_.draw_text_centered(kCanvasW / 2, 320, "Spin to change · Press to confirm digit",
                             Canvas::TextRole::Secondary, Gray::G1);
  if (now_ms_ < error_until_ms_) {
    canvas_.draw_text_centered(kCanvasW / 2, 360, error_msg_.c_str(), Canvas::TextRole::Body, Gray::G0);
  }
  if (now_ms_ < pin_lockout_until_ms_) {
    canvas_.draw_text_centered(kCanvasW / 2, 400, "Try again in 30 seconds", Canvas::TextRole::Body, Gray::G0);
  }
}

void App::handle_pin(InputEvent e) {
  if (now_ms_ < pin_lockout_until_ms_) return;
  if (e == InputEvent::Back) {
    if (pin_entry_.empty()) {
      nav_.pop();
      after_nav(true);
    } else {
      pin_entry_.pop_back();
      dirty_ = true;
    }
    return;
  }

  static char working = '0';

  if (e == InputEvent::Up) {
    working = static_cast<char>('0' + ((working - '0' + 9) % 10));
    if (pin_entry_.size() == static_cast<size_t>(focus_.index)) {
      pin_entry_.push_back(working);
    } else if (focus_.index < static_cast<int>(pin_entry_.size())) {
      pin_entry_[focus_.index] = working;
    }
    dirty_ = true;
    return;
  }
  if (e == InputEvent::Down) {
    working = static_cast<char>('0' + ((working - '0' + 1) % 10));
    if (pin_entry_.size() == static_cast<size_t>(focus_.index)) {
      pin_entry_.push_back(working);
    } else if (focus_.index < static_cast<int>(pin_entry_.size())) {
      pin_entry_[focus_.index] = working;
    }
    dirty_ = true;
    return;
  }
  if (e == InputEvent::Select) {
    if (pin_entry_.size() == static_cast<size_t>(focus_.index)) {
      pin_entry_.push_back(working);
    }
    if (static_cast<int>(pin_entry_.size()) >= cfg_.pin_length) {
      if (verify_pin(cfg_, pin_entry_)) {
        pin_fail_count_ = 0;
        working = '0';
        go_home();
      } else {
        error_msg_ = "Incorrect PIN";
        error_until_ms_ = now_ms_ + 2000;
        ++pin_fail_count_;
        pin_entry_.clear();
        focus_.index = 0;
        working = '0';
        if (pin_fail_count_ >= 5) {
          pin_lockout_until_ms_ = now_ms_ + 30000;
          pin_fail_count_ = 0;
        }
        dirty_ = true;
      }
    } else {
      focus_.index = static_cast<int>(pin_entry_.size());
      working = '0';
      dirty_ = true;
    }
  }
}

}  // namespace pocket