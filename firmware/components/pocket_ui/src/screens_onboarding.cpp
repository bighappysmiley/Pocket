#include "pocket/app.hpp"
#include <cstdio>
#include <string>

namespace pocket {

static const char* kTimezones[] = {"America/New_York", "America/Chicago", "America/Denver",
                                   "America/Los_Angeles", "America/Phoenix", "UTC", "Europe/London"};
static constexpr int kTzCount = 7;

static const char* kWifiPasswordCharset =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ._-!@#$%^&*()+=?";

static void draw_step(Canvas& c, int step) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "Step %d of 7", step);
  c.draw_text(kSideMargin, 36, buf, Canvas::TextRole::Secondary, Gray::G1);
}

void App::render_onboarding() {
  draw_status_bar();
  const ScreenId s = nav_.current();
  const int step = onboarding_step_of(s);
  if (step > 0) draw_step(canvas_, step);

  switch (s) {
    case ScreenId::OnboardingWelcome: {
      // Part B §3.2 — intro only, no name field
      canvas_.draw_text(kSideMargin, 80, "Welcome to Pocket", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 120, "A calm place for notes, lists,", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 144, "and daily essentials.", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 190, "Next, connect to Wi-Fi.", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_focus_tile(kSideMargin, 720, kCanvasW - 32, 48, "Continue", Canvas::TextRole::Body);
      break;
    }
    case ScreenId::OnboardingWifiList: {
      canvas_.draw_text(kSideMargin, 80, "Wi-Fi", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 110, "Pocket needs Wi-Fi to finish setup.", Canvas::TextRole::Secondary,
                        Gray::G1);
      if (wifi_networks_.empty()) {
        canvas_.draw_text(kSideMargin, 200, "Looking for networks…", Canvas::TextRole::Body, Gray::G1);
      }
      focus_.count = static_cast<int>(wifi_networks_.size()) + 1;
      for (int i = 0; i < static_cast<int>(wifi_networks_.size()); ++i) {
        const int y = 160 + i * 52;
        if (i == focus_.index) {
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, wifi_networks_[i], Canvas::TextRole::Body);
        } else {
          canvas_.draw_text(kSideMargin + 8, y + 14, wifi_networks_[i], Canvas::TextRole::Body, Gray::G0);
        }
      }
      {
        const int y = 160 + static_cast<int>(wifi_networks_.size()) * 52;
        if (focus_.index == static_cast<int>(wifi_networks_.size())) {
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, "Rescan", Canvas::TextRole::Body);
        } else {
          canvas_.draw_text(kSideMargin + 8, y + 14, "Rescan", Canvas::TextRole::Body, Gray::G0);
        }
      }
      break;
    }
    case ScreenId::OnboardingWifiPassword: {
      canvas_.draw_text(kSideMargin, 80, "Password", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 110, "Spin · Press to enter character", Canvas::TextRole::Secondary, Gray::G1);
      std::string shown = wifi_show_pw_ ? wifi_password_ : std::string(wifi_password_.size(), '*');
      if (shown.empty()) shown = " ";
      canvas_.draw_text(kSideMargin, 160, shown, Canvas::TextRole::Body, Gray::G0);
      char ch[2] = {kWifiPasswordCharset[charset_index_ % static_cast<int>(std::char_traits<char>::length(kWifiPasswordCharset))],
                    0};
      focus_.count = 4;  // char, Delete, Show/Hide, Connect
      const char* actions[] = {ch, "Delete", wifi_show_pw_ ? "Hide" : "Show", "Connect"};
      for (int i = 0; i < 4; ++i) {
        int y = 280 + i * 56;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, actions[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + 14, actions[i], Canvas::TextRole::Body, Gray::G0);
      }
      if (now_ms_ < error_until_ms_) {
        canvas_.draw_text(kSideMargin, 520, error_msg_, Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingWifiConnecting: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "Connecting to %s…", cfg_.wifi_ssid.c_str());
      canvas_.draw_text(kSideMargin, 200, buf, Canvas::TextRole::Body, Gray::G0);
      break;
    }
    case ScreenId::OnboardingCompanionQr: {
      canvas_.draw_text(kSideMargin, 80, "Set up with your phone", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 112, "Name your Pocket, sync Notes, and", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 136, "manage Pocket Cloud in the app.", Canvas::TextRole::Body, Gray::G0);
      // QR placeholder block (device draws real QR via display driver)
      canvas_.stroke_rect(140, 200, 200, 200, Gray::G0);
      canvas_.draw_text_centered(kCanvasW / 2, 280, "QR", Canvas::TextRole::Body, Gray::G1);
      char code_line[48];
      std::snprintf(code_line, sizeof(code_line), "Code · %s", pair_code_.c_str());
      canvas_.draw_text_centered(kCanvasW / 2, 420, code_line, Canvas::TextRole::Secondary, Gray::G1);
      if (pair_status_ == "expired") {
        canvas_.draw_text_centered(kCanvasW / 2, 450, "Code expired", Canvas::TextRole::Body, Gray::G0);
      } else if (pair_status_ == "claimed") {
        canvas_.draw_text_centered(kCanvasW / 2, 450, "Linked", Canvas::TextRole::Body, Gray::G0);
      } else {
        canvas_.draw_text_centered(kCanvasW / 2, 450, "Expires in 10 minutes", Canvas::TextRole::Secondary, Gray::G1);
      }
      focus_.count = 3;
      const char* acts[] = {"Continue on phone", "Skip for now", "Refresh code"};
      for (int i = 0; i < 3; ++i) {
        int y = 500 + i * 56;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, acts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + 14, acts[i], Canvas::TextRole::Body, Gray::G0);
      }
      canvas_.draw_text(kSideMargin, 680, "You can link later in Settings.", Canvas::TextRole::Secondary, Gray::G1);
      break;
    }
    case ScreenId::OnboardingPinLength: {
      canvas_.draw_text(kSideMargin, 80, "Set a PIN", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 120, "You'll use this PIN to unlock Pocket.", Canvas::TextRole::Secondary,
                        Gray::G1);
      focus_.count = 2;
      const char* opts[] = {"4 digits", "6 digits"};
      for (int i = 0; i < 2; ++i) {
        int y = 200 + i * 64;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 56, opts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + 16, opts[i], Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingPinSet:
    case ScreenId::OnboardingPinConfirm: {
      const bool confirm = s == ScreenId::OnboardingPinConfirm;
      canvas_.draw_text(kSideMargin, 80, confirm ? "Confirm PIN" : "Create PIN", Canvas::TextRole::ScreenTitle,
                        Gray::G0);
      // Reuse digit display
      const int n = cfg_.pin_length;
      std::string& entry = pin_entry_;
      const int slot_w = 48;
      const int total = n * slot_w + (n - 1) * 12;
      int x0 = (kCanvasW - total) / 2;
      for (int i = 0; i < n; ++i) {
        int x = x0 + i * (slot_w + 12);
        bool focused = static_cast<int>(entry.size()) == i;
        char dig = (i < static_cast<int>(entry.size())) ? entry[i] : '0';
        char ds[2] = {dig, 0};
        if (focused)
          canvas_.draw_focus_tile(x, 200, slot_w, 64, ds, Canvas::TextRole::PinDigit);
        else if (i < static_cast<int>(entry.size()))
          canvas_.draw_text(x + 8, 212, ds, Canvas::TextRole::PinDigit, Gray::G0);
        else
          canvas_.hline(x, 250, slot_w, Gray::G2);
      }
      if (now_ms_ < error_until_ms_) {
        canvas_.draw_text_centered(kCanvasW / 2, 320, error_msg_, Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingTimezone: {
      canvas_.draw_text(kSideMargin, 80, "Clock", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 120, "Time zone", Canvas::TextRole::Secondary, Gray::G1);
      focus_.count = kTzCount + 3;  // zones + 12h + 24h + Continue
      for (int i = 0; i < kTzCount; ++i) {
        int y = 150 + i * 40;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 36, kTimezones[i], Canvas::TextRole::Secondary);
        else
          canvas_.draw_text(kSideMargin + 8, y + 8, kTimezones[i], Canvas::TextRole::Secondary,
                            i == onboarding_tz_index_ ? Gray::G0 : Gray::G1);
      }
      int y = 150 + kTzCount * 40 + 20;
      if (focus_.index == kTzCount)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 40, "12-hour", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + 10, "12-hour", Canvas::TextRole::Body, Gray::G0);
      y += 48;
      if (focus_.index == kTzCount + 1)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 40, "24-hour", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + 10, "24-hour", Canvas::TextRole::Body, Gray::G0);
      y += 56;
      if (focus_.index == kTzCount + 2)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, "Continue", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + 14, "Continue", Canvas::TextRole::Body, Gray::G0);
      break;
    }
    case ScreenId::OnboardingMicTest: {
      canvas_.draw_text(kSideMargin, 80, "Voice", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 120, "Hold BOOT and say something.", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 160, "Speech recognition: Cloud", Canvas::TextRole::Secondary, Gray::G1);
      if (ptt_active_) {
        canvas_.draw_text(kSideMargin, 220, "Listening…", Canvas::TextRole::Body, Gray::G0);
      } else if (!mic_result_.empty()) {
        std::string line = "Heard: \"" + mic_result_ + "\"";
        canvas_.draw_text(kSideMargin, 220, line, Canvas::TextRole::Body, Gray::G0);
      }
      focus_.count = 2;
      const char* acts[] = {"Try again", "Continue"};
      for (int i = 0; i < 2; ++i) {
        int y = 400 + i * 56;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, acts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + 14, acts[i], Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingDone: {
      canvas_.draw_text_centered(kCanvasW / 2, 200, "You're ready", Canvas::TextRole::ScreenTitle, Gray::G0);
      if (cfg_.companion_linked) {
        canvas_.draw_text_centered(kCanvasW / 2, 260, "Your phone is linked. Name and", Canvas::TextRole::Body,
                                   Gray::G0);
        canvas_.draw_text_centered(kCanvasW / 2, 284, "Cloud settings are in the app.", Canvas::TextRole::Body,
                                   Gray::G0);
      } else {
        canvas_.draw_text_centered(kCanvasW / 2, 260, "You can link the Pocket app", Canvas::TextRole::Body, Gray::G0);
        canvas_.draw_text_centered(kCanvasW / 2, 284, "anytime in Settings.", Canvas::TextRole::Body, Gray::G0);
      }
      canvas_.draw_text_centered(kCanvasW / 2, 720, "Press to go Home", Canvas::TextRole::Secondary, Gray::G1);
      break;
    }
    default:
      break;
  }
}

void App::handle_onboarding(InputEvent e) {
  ScreenId s = nav_.current();

  if (e == InputEvent::Back) {
    // Wi‑Fi password: first Back exits character edit, second leaves screen
    if (s == ScreenId::OnboardingWifiPassword && wifi_char_editing_) {
      wifi_char_editing_ = false;
      dirty_ = true;
      return;
    }
    switch (s) {
      case ScreenId::OnboardingWelcome:
        break;
      case ScreenId::OnboardingWifiPassword:
        nav_.replace(ScreenId::OnboardingWifiList);
        after_nav(true);
        break;
      case ScreenId::OnboardingWifiList:
        nav_.replace(ScreenId::OnboardingWelcome);
        after_nav(true);
        break;
      case ScreenId::OnboardingCompanionQr:
        nav_.replace(ScreenId::OnboardingWifiList);
        after_nav(true);
        break;
      case ScreenId::OnboardingPinLength:
        nav_.replace(ScreenId::OnboardingCompanionQr);
        after_nav(true);
        break;
      case ScreenId::OnboardingPinSet:
        nav_.replace(ScreenId::OnboardingPinLength);
        after_nav(true);
        break;
      case ScreenId::OnboardingPinConfirm:
        nav_.replace(ScreenId::OnboardingPinSet);
        pin_entry_.clear();
        after_nav(true);
        break;
      case ScreenId::OnboardingTimezone:
        nav_.replace(ScreenId::OnboardingPinConfirm);
        after_nav(true);
        break;
      case ScreenId::OnboardingMicTest:
        nav_.replace(ScreenId::OnboardingTimezone);
        after_nav(true);
        break;
      case ScreenId::OnboardingDone:
        nav_.replace(ScreenId::OnboardingMicTest);
        after_nav(true);
        break;
      default:
        break;
    }
    return;
  }

  if (s == ScreenId::OnboardingWelcome && e == InputEvent::Select) {
    if (cfg_.device_name.empty()) cfg_.device_name = "Pocket";
    store_.save(cfg_);
    wifi_networks_ = wifi_.scan();
    focus_.index = 0;
    nav_.replace(ScreenId::OnboardingWifiList);
    after_nav(true);
    return;
  }

  if (s == ScreenId::OnboardingWifiList) {
    focus_.count = static_cast<int>(wifi_networks_.size()) + 1;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index >= static_cast<int>(wifi_networks_.size())) {
        wifi_networks_ = wifi_.scan();
        dirty_ = true;
      } else {
        cfg_.wifi_ssid = wifi_networks_[focus_.index];
        wifi_password_.clear();
        charset_index_ = 0;
        focus_.index = 0;
        wifi_char_editing_ = true;
        nav_.replace(ScreenId::OnboardingWifiPassword);
        after_nav(true);
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingWifiPassword) {
    const int charset_len = static_cast<int>(std::char_traits<char>::length(kWifiPasswordCharset));
    focus_.count = 4;
    // Editing the character picker: spin changes value; Back exits edit to action focus.
    if (wifi_char_editing_ && focus_.index == 0) {
      if (e == InputEvent::Back) {
        wifi_char_editing_ = false;
        dirty_ = true;
        return;
      }
      if (e == InputEvent::Up) {
        charset_index_ = (charset_index_ + charset_len - 1) % charset_len;
        dirty_ = true;
        return;
      }
      if (e == InputEvent::Down) {
        charset_index_ = (charset_index_ + 1) % charset_len;
        dirty_ = true;
        return;
      }
      if (e == InputEvent::Select) {
        if (wifi_password_.size() < 63) wifi_password_.push_back(kWifiPasswordCharset[charset_index_]);
        dirty_ = true;
        return;
      }
    }
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        wifi_char_editing_ = true;
        dirty_ = true;
      } else if (focus_.index == 1) {
        if (!wifi_password_.empty()) wifi_password_.pop_back();
        dirty_ = true;
      } else if (focus_.index == 2) {
        wifi_show_pw_ = !wifi_show_pw_;
        dirty_ = true;
      } else {
        nav_.replace(ScreenId::OnboardingWifiConnecting);
        after_nav(true);
        const bool ok = wifi_.connect(cfg_.wifi_ssid, wifi_password_);
        if (ok) {
          store_.save(cfg_);
          pair_code_ = cloud_.create_pair_session(cfg_.device_id);
          pair_expires_ms_ = now_ms_ + 10 * 60 * 1000;
          pair_status_ = "pending";
          focus_.index = 0;
          nav_.replace(ScreenId::OnboardingCompanionQr);
          after_nav(true);
        } else {
          error_msg_ = "Couldn't connect. Check the password.";
          error_until_ms_ = now_ms_ + 3000;
          wifi_char_editing_ = true;
          focus_.index = 0;
          nav_.replace(ScreenId::OnboardingWifiPassword);
          after_nav(true);
        }
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingCompanionQr) {
    if (pair_status_ == "claimed") {
      focus_.index = 0;
      nav_.replace(ScreenId::OnboardingPinLength);
      after_nav(true);
      return;
    }
    focus_.count = 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 1) {
        // Skip for now
        nav_.replace(ScreenId::OnboardingPinLength);
        after_nav(true);
      } else if (focus_.index == 2 || pair_status_ == "expired") {
        pair_code_ = cloud_.create_pair_session(cfg_.device_id);
        pair_expires_ms_ = now_ms_ + 10 * 60 * 1000;
        pair_status_ = "pending";
        dirty_ = true;
      }
      // Continue on phone = soft affirmation
    }
    return;
  }

  if (s == ScreenId::OnboardingPinLength) {
    focus_.count = 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      cfg_.pin_length = focus_.index == 0 ? 4 : 6;
      pin_entry_.clear();
      focus_.index = 0;
      nav_.replace(ScreenId::OnboardingPinSet);
      after_nav(true);
    }
    return;
  }

  if (s == ScreenId::OnboardingPinSet || s == ScreenId::OnboardingPinConfirm) {
    static char working = '0';
    if (e == InputEvent::Up) {
      working = static_cast<char>('0' + ((working - '0' + 9) % 10));
      if (pin_entry_.size() <= static_cast<size_t>(focus_.index)) {
        if (pin_entry_.size() == static_cast<size_t>(focus_.index)) pin_entry_.push_back(working);
      } else {
        pin_entry_[focus_.index] = working;
      }
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      working = static_cast<char>('0' + ((working - '0' + 1) % 10));
      if (pin_entry_.size() == static_cast<size_t>(focus_.index))
        pin_entry_.push_back(working);
      else if (focus_.index < static_cast<int>(pin_entry_.size()))
        pin_entry_[focus_.index] = working;
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (pin_entry_.size() == static_cast<size_t>(focus_.index)) pin_entry_.push_back(working);
      if (static_cast<int>(pin_entry_.size()) >= cfg_.pin_length) {
        if (s == ScreenId::OnboardingPinSet) {
          pin_pending_ = pin_entry_;
          pin_entry_.clear();
          focus_.index = 0;
          working = '0';
          nav_.replace(ScreenId::OnboardingPinConfirm);
          after_nav(true);
        } else {
          if (pin_entry_ == pin_pending_) {
            set_pin(cfg_, pin_entry_);
            store_.save(cfg_);
            pin_entry_.clear();
            pin_pending_.clear();
            focus_.index = onboarding_tz_index_;
            nav_.replace(ScreenId::OnboardingTimezone);
            after_nav(true);
          } else {
            error_msg_ = "PINs don't match. Try again.";
            error_until_ms_ = now_ms_ + 2500;
            pin_entry_.clear();
            focus_.index = 0;
            working = '0';
            nav_.replace(ScreenId::OnboardingPinSet);
            after_nav(true);
          }
        }
      } else {
        focus_.index = static_cast<int>(pin_entry_.size());
        working = '0';
        dirty_ = true;
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingTimezone) {
    focus_.count = kTzCount + 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index < kTzCount) {
        onboarding_tz_index_ = focus_.index;
        cfg_.tz_id = kTimezones[focus_.index];
        dirty_ = true;
      } else if (focus_.index == kTzCount) {
        cfg_.time_format = 12;
        dirty_ = true;
      } else if (focus_.index == kTzCount + 1) {
        cfg_.time_format = 24;
        dirty_ = true;
      } else {
        store_.save(cfg_);
        focus_.index = 1;
        mic_result_.clear();
        nav_.replace(ScreenId::OnboardingMicTest);
        after_nav(true);
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingMicTest) {
    if (e == InputEvent::PttStart) {
      ptt_active_ = true;
      dirty_ = true;
    } else if (e == InputEvent::PttStop) {
      ptt_active_ = false;
      std::vector<uint8_t> fake;
      mic_result_ = cloud_.stt_transcribe(fake);
      if (mic_result_.empty()) mic_result_.clear();
      dirty_ = true;
    } else {
      focus_.count = 2;
      if (e == InputEvent::Up) {
        focus_.move(-1);
        dirty_ = true;
      } else if (e == InputEvent::Down) {
        focus_.move(1);
        dirty_ = true;
      } else if (e == InputEvent::Select) {
        if (focus_.index == 0) {
          mic_result_.clear();
          dirty_ = true;
        } else {
          cfg_.stt_path = 0;
          store_.save(cfg_);
          nav_.replace(ScreenId::OnboardingDone);
          after_nav(true);
        }
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingDone && e == InputEvent::Select) {
    cfg_.onboarding_complete = true;
    if (cfg_.device_name.empty()) cfg_.device_name = "Pocket";
    store_.save(cfg_);
    go_home();
  }
}

}  // namespace pocket