#include "pocket/app.hpp"
#include "pocket/cloud_client.hpp"
#include <algorithm>
#include <cstdio>
#include <string>

namespace pocket {

static const char* kTimezones[] = {"America/New_York", "America/Chicago", "America/Denver",
                                   "America/Los_Angeles", "America/Phoenix", "UTC", "Europe/London"};
static constexpr int kTzCount = 7;

static void draw_step(Canvas& c, int step) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "Step %d of 8", step);
  c.draw_text(kSideMargin, 36, buf, Canvas::TextRole::Secondary, Gray::G1);
}

void App::render_onboarding() {
  draw_status_bar();
  const ScreenId s = nav_.current();
  const int step = onboarding_step_of(s);
  if (step > 0) draw_step(canvas_, step);

  switch (s) {
    case ScreenId::OnboardingWelcome: {
      // Brand first; keep lines short so type never clips the 480px width.
      canvas_.draw_text(kSideMargin, 72, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
      canvas_.draw_text(kSideMargin, 148, "Welcome", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 210, "A calm place for notes,", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 246, "lists, and daily essentials.", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 310, "Next, get the Pocket app.", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_focus_tile(kSideMargin, 700, kCanvasW - 32, 56, "Continue", Canvas::TextRole::Body);
      break;
    }
    case ScreenId::OnboardingCompanionDownload: {
      canvas_.draw_text(kSideMargin, 72, "Pocket app", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 118, "Download the companion app", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text(kSideMargin, 148, "on your phone to finish setup.", Canvas::TextRole::Body, Gray::G0);

      const int qr_size = 220;
      const int qr_x = (kCanvasW - qr_size) / 2;
      const int qr_y = 196;
      const std::string url = companion_download_url();
      if (!canvas_.draw_qr(qr_x, qr_y, qr_size, url)) {
        canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size / 2 - 8, "QR error", Canvas::TextRole::Body,
                                   Gray::G1);
      }
      canvas_.draw_text_centered(kCanvasW / 2, 436, "Scan to open Pocket on your phone", Canvas::TextRole::Secondary,
                                 Gray::G1);
      canvas_.draw_text_centered(kCanvasW / 2, 464, "Setup needs the app — no skip.", Canvas::TextRole::Secondary,
                                 Gray::G1);
      focus_.count = 1;
      canvas_.draw_focus_tile(kSideMargin, 520, kCanvasW - 32, 56, "Continue", Canvas::TextRole::Body);
      break;
    }
    case ScreenId::OnboardingWifiList: {
      canvas_.draw_text(kSideMargin, 72, "Wi-Fi", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 118, "Pick a network. Password is", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_text(kSideMargin, 142, "entered in the Pocket app.", Canvas::TextRole::Secondary, Gray::G1);
      constexpr int kRowH = 52;
      constexpr int kListTop = 190;
      constexpr int kMaxVisible = 6;
      if (wifi_networks_.empty()) {
        canvas_.draw_text(kSideMargin, 176, "No networks found.", Canvas::TextRole::Body, Gray::G1);
      }
      const int shown = std::min(static_cast<int>(wifi_networks_.size()), kMaxVisible);
      focus_.count = shown + 1;  // + Rescan only
      for (int i = 0; i < shown; ++i) {
        const int y = kListTop + i * kRowH;
        if (i == focus_.index) {
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kRowH - 8, wifi_networks_[i],
                                  Canvas::TextRole::Body);
        } else {
          canvas_.draw_text_fit(kSideMargin + 8, y + 12, kCanvasW - 48, wifi_networks_[i],
                                Canvas::TextRole::Body, Gray::G0);
        }
      }
      {
        const int y = wifi_networks_.empty() ? 220 : (kListTop + shown * kRowH);
        if (focus_.index == shown) {
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kRowH - 8, "Rescan", Canvas::TextRole::Body);
        } else {
          canvas_.draw_text(kSideMargin + 8, y + 12, "Rescan", Canvas::TextRole::Body, Gray::G0);
        }
      }
      break;
    }
    case ScreenId::OnboardingWifiPassword: {
      // SoftAP wait — password is typed on the phone, never on the dial.
      canvas_.draw_text(kSideMargin, 72, "Phone setup", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 118, "Join this Wi-Fi on your phone,", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_text(kSideMargin, 142, "then enter the password there.", Canvas::TextRole::Secondary, Gray::G1);

      char ap_line[48];
      std::snprintf(ap_line, sizeof(ap_line), "%s", wifi_ap_ssid_.empty() ? "Pocket-...." : wifi_ap_ssid_.c_str());
      canvas_.draw_text_centered(kCanvasW / 2, 190, ap_line, Canvas::TextRole::Body, Gray::G0);

      if (!cfg_.wifi_ssid.empty()) {
        char net[64];
        std::snprintf(net, sizeof(net), "Network: %s", cfg_.wifi_ssid.c_str());
        canvas_.draw_text_fit(kSideMargin, 230, kCanvasW - 32, net, Canvas::TextRole::Secondary, Gray::G1);
      } else {
        canvas_.draw_text(kSideMargin, 230, "Pick any network on your phone.", Canvas::TextRole::Secondary,
                          Gray::G1);
      }

      const int qr_size = 160;
      const int qr_x = (kCanvasW - qr_size) / 2;
      const int qr_y = 280;
      const std::string url = companion_wifi_setup_url();
      if (!canvas_.draw_qr(qr_x, qr_y, qr_size, url)) {
        canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
      }
      canvas_.draw_text_centered(kCanvasW / 2, 456, "Open Pocket app → Wi-Fi setup", Canvas::TextRole::Secondary,
                                 Gray::G1);
      canvas_.draw_text_centered(kCanvasW / 2, 484, "Waiting for your phone…", Canvas::TextRole::Body, Gray::G0);

      focus_.count = 2;
      const char* actions[] = {"Waiting…", "Cancel"};
      for (int i = 0; i < 2; ++i) {
        int y = 540 + i * 56;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, actions[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + 14, actions[i], Canvas::TextRole::Body, Gray::G0);
      }
      if (now_ms_ < error_until_ms_) {
        canvas_.draw_text_fit(kSideMargin, 660, kCanvasW - 32, error_msg_, Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingWifiConnecting: {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "Connecting to %s...", cfg_.wifi_ssid.c_str());
      canvas_.draw_text_fit(kSideMargin, 200, kCanvasW - 32, buf, Canvas::TextRole::Body, Gray::G0);
      break;
    }
    case ScreenId::OnboardingCompanionQr: {
      canvas_.draw_text(kSideMargin, 72, "Link your phone", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, 118, "Scan the code in the Pocket app,", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_text(kSideMargin, 142, "or enter the code below.", Canvas::TextRole::Secondary, Gray::G1);

      const int qr_size = 220;
      const int qr_x = (kCanvasW - qr_size) / 2;
      const int qr_y = 176;
      if (!pair_code_.empty()) {
        const std::string url = companion_pair_url(pair_code_);
        if (!canvas_.draw_qr(qr_x, qr_y, qr_size, url)) {
          canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
          canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size / 2 - 8, "QR error", Canvas::TextRole::Body,
                                     Gray::G1);
        }
      } else {
        canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size / 2 - 8, "No code", Canvas::TextRole::Body, Gray::G1);
      }

      char code_line[48];
      std::snprintf(code_line, sizeof(code_line), "Code: %s", pair_code_.c_str());
      canvas_.draw_text_centered(kCanvasW / 2, 410, code_line, Canvas::TextRole::Body, Gray::G0);
      if (pair_status_ == "expired") {
        canvas_.draw_text_centered(kCanvasW / 2, 444, "Code expired", Canvas::TextRole::Secondary, Gray::G0);
      } else if (pair_status_ == "claimed") {
        canvas_.draw_text_centered(kCanvasW / 2, 444, "Linked", Canvas::TextRole::Secondary, Gray::G0);
      } else {
        canvas_.draw_text_centered(kCanvasW / 2, 444, "Expires in 10 minutes", Canvas::TextRole::Secondary, Gray::G1);
      }
      focus_.count = 2;
      const char* acts[] = {"Waiting for link…", "Refresh code"};
      for (int i = 0; i < 2; ++i) {
        int y = 500 + i * 56;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, 48, acts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + 14, acts[i], Canvas::TextRole::Body, Gray::G0);
      }
      canvas_.draw_text(kSideMargin, 640, "The Pocket app is required to continue.", Canvas::TextRole::Secondary,
                        Gray::G1);
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
        canvas_.draw_text(kSideMargin, 220, "Listening...", Canvas::TextRole::Body, Gray::G0);
      } else if (!mic_result_.empty()) {
        std::string line = "Heard: \"" + mic_result_ + "\"";
        canvas_.draw_text_fit(kSideMargin, 220, kCanvasW - 32, line, Canvas::TextRole::Body, Gray::G0);
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
      canvas_.draw_text_centered(kCanvasW / 2, 260, "Your phone is linked. Name and", Canvas::TextRole::Body,
                                 Gray::G0);
      canvas_.draw_text_centered(kCanvasW / 2, 284, "Cloud settings are in the app.", Canvas::TextRole::Body,
                                 Gray::G0);
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
    switch (s) {
      case ScreenId::OnboardingWelcome:
        break;
      case ScreenId::OnboardingWifiPassword:
        wifi_.stop_provision();
        nav_.replace(ScreenId::OnboardingWifiList);
        after_nav();
        break;
      case ScreenId::OnboardingWifiList:
        if (cfg_.onboarding_complete) {
          nav_.replace(ScreenId::SettingsWifi);
        } else {
          nav_.replace(ScreenId::OnboardingCompanionDownload);
        }
        after_nav();
        break;
      case ScreenId::OnboardingCompanionDownload:
        nav_.replace(ScreenId::OnboardingWelcome);
        after_nav();
        break;
      case ScreenId::OnboardingCompanionQr:
        if (cfg_.onboarding_complete) {
          nav_.replace(ScreenId::SettingsCloud);
        } else {
          nav_.replace(ScreenId::OnboardingWifiList);
        }
        after_nav();
        break;
      case ScreenId::OnboardingPinLength:
        nav_.replace(ScreenId::OnboardingCompanionQr);
        after_nav();
        break;
      case ScreenId::OnboardingPinSet:
        nav_.replace(ScreenId::OnboardingPinLength);
        after_nav();
        break;
      case ScreenId::OnboardingPinConfirm:
        nav_.replace(ScreenId::OnboardingPinSet);
        pin_entry_.clear();
        after_nav();
        break;
      case ScreenId::OnboardingTimezone:
        nav_.replace(ScreenId::OnboardingPinConfirm);
        after_nav();
        break;
      case ScreenId::OnboardingMicTest:
        nav_.replace(ScreenId::OnboardingTimezone);
        after_nav();
        break;
      case ScreenId::OnboardingDone:
        nav_.replace(ScreenId::OnboardingMicTest);
        after_nav();
        break;
      default:
        break;
    }
    return;
  }

  if (s == ScreenId::OnboardingWelcome && e == InputEvent::Select) {
    if (cfg_.device_name.empty()) cfg_.device_name = "Pocket";
    store_.save(cfg_);
    focus_.index = 0;
    nav_.replace(ScreenId::OnboardingCompanionDownload);
    after_nav();
    return;
  }

  if (s == ScreenId::OnboardingCompanionDownload && e == InputEvent::Select) {
    wifi_networks_ = wifi_.scan();
    focus_.index = 0;
    nav_.replace(ScreenId::OnboardingWifiList);
    after_nav();
    return;
  }

  if (s == ScreenId::OnboardingWifiList) {
    constexpr int kMaxVisible = 6;
    const int shown = std::min(static_cast<int>(wifi_networks_.size()), kMaxVisible);
    focus_.count = shown + 1;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index >= shown) {
        wifi_networks_ = wifi_.scan();
        dirty_ = true;
      } else {
        cfg_.wifi_ssid = wifi_networks_[focus_.index];
        wifi_password_.clear();
        std::string ap;
        if (!wifi_.start_provision(cfg_.wifi_ssid, &ap)) {
          error_msg_ = "Couldn't start phone setup.";
          error_until_ms_ = now_ms_ + 3000;
          dirty_ = true;
          return;
        }
        wifi_ap_ssid_ = ap;
        last_wifi_prov_poll_ms_ = 0;
        focus_.index = 0;
        nav_.replace(ScreenId::OnboardingWifiPassword);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingWifiPassword) {
    focus_.count = 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 1) {
        wifi_.stop_provision();
        nav_.replace(ScreenId::OnboardingWifiList);
        after_nav();
      }
      // Waiting… — credentials arrive via App::tick SoftAP poll
    }
    return;
  }

  if (s == ScreenId::OnboardingCompanionQr) {
    if (pair_status_ == "claimed") {
      focus_.index = 0;
      if (cfg_.onboarding_complete) {
        nav_.replace(ScreenId::SettingsCloud);
      } else {
        nav_.replace(ScreenId::OnboardingPinLength);
      }
      after_nav();
      return;
    }
    focus_.count = 2;  // Waiting… · Refresh code — no Skip
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 1 || pair_status_ == "expired") {
        pair_code_ = cloud_.create_pair_session(cfg_.device_id);
        pair_expires_ms_ = now_ms_ + 10 * 60 * 1000;
        pair_status_ = "pending";
        last_pair_poll_ms_ = 0;
        dirty_ = true;
      }
      // index 0 = waiting — link arrives via tick poll
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
      after_nav();
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
          after_nav();
        } else {
          if (pin_entry_ == pin_pending_) {
            set_pin(cfg_, pin_entry_);
            store_.save(cfg_);
            pin_entry_.clear();
            pin_pending_.clear();
            focus_.index = onboarding_tz_index_;
            nav_.replace(ScreenId::OnboardingTimezone);
            after_nav();
          } else {
            error_msg_ = "PINs don't match. Try again.";
            error_until_ms_ = now_ms_ + 2500;
            pin_entry_.clear();
            focus_.index = 0;
            working = '0';
            nav_.replace(ScreenId::OnboardingPinSet);
            after_nav();
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
        after_nav();
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
          after_nav();
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