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
  std::snprintf(buf, sizeof(buf), "Step %d of 9", step);
  // Own band under the status bar — never share y with titles.
  c.draw_text(kSideMargin, kContentTop, buf, Canvas::TextRole::Secondary, Gray::G1);
}

/** Title y for onboarding screens that show a step indicator. */
static int title_y(int step) { return step > 0 ? kOnboardingTitleY : kContentTop; }

void App::render_onboarding() {
  draw_status_bar();
  const ScreenId s = nav_.current();
  const int step = onboarding_step_of(s);
  if (step > 0) draw_step(canvas_, step);
  const int ty = title_y(step);
  constexpr int kWrapW = kContentW;
  constexpr int kLineGap = 6;

  switch (s) {
    case ScreenId::OnboardingWelcome: {
      canvas_.draw_text(kSideMargin, ty, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
      canvas_.draw_text(kSideMargin, ty + 52, "Welcome", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 108, kWrapW, kLineGap,
                                        "Notes, lists, and daily tools — quiet, focused, always with you.",
                                        Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text_wrapped(kSideMargin, y + 12, kWrapW, kLineGap,
                                "A short setup with the Pocket app gets you online and linked.",
                                Canvas::TextRole::Secondary, Gray::G1);
      focus_.count = 1;
      canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kCanvasW - 32, kFocusRowH, "Begin setup", Canvas::TextRole::Body);
      break;
    }
    case ScreenId::OnboardingCompanionDownload: {
      canvas_.draw_text(kSideMargin, ty, "Get the Pocket app", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap,
                                        "Scan to open Pocket on your phone.", Canvas::TextRole::Secondary, Gray::G1);

      const int qr_size = 168;
      const int qr_x = (kCanvasW - qr_size) / 2;
      const int qr_y = std::max(y + 16, 200);
      const std::string url = companion_download_url();
      if (!canvas_.draw_qr(qr_x, qr_y, qr_size, url)) {
        canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size / 2 - 4, "QR unavailable", Canvas::TextRole::Body,
                                   Gray::G1);
      }
      canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 20, "or go to", Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_text_wrapped(kSideMargin, qr_y + qr_size + 52, kWrapW, kLineGap, companion_display_origin(),
                                Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 100, "Required to finish setup",
                                 Canvas::TextRole::Secondary, Gray::G1);
      focus_.count = 1;
      canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kCanvasW - 32, kFocusRowH, "Continue", Canvas::TextRole::Body);
      break;
    }
    case ScreenId::OnboardingSdCard: {
      canvas_.draw_text(kSideMargin, ty, "microSD card", Canvas::TextRole::ScreenTitle, Gray::G0);
      if (sd_waiting_eject_) {
        int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap, "Remove the card to continue.",
                                          Canvas::TextRole::Body, Gray::G0);
        canvas_.draw_text(kSideMargin, y + 8, "Waiting for eject…", Canvas::TextRole::Secondary, Gray::G1);
        focus_.count = 1;
        canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kCanvasW - 32, kFocusRowH, "Checking…", Canvas::TextRole::Body);
        break;
      }
      if (sd_kind_ == SdContentKind::Absent) {
        int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap, "No card detected.",
                                          Canvas::TextRole::Body, Gray::G0);
        y = canvas_.draw_text_wrapped(kSideMargin, y + 8, kWrapW, kLineGap,
                                      "Insert a card for storage, or continue.", Canvas::TextRole::Secondary,
                                      Gray::G1);
        focus_.count = 2;
        const char* acts[] = {"Check again", "Continue without card"};
        for (int i = 0; i < 2; ++i) {
          const int row_y = y + 24 + i * kRowPitch;
          if (i == focus_.index)
            canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
          else
            canvas_.draw_text(kSideMargin + 8, row_y + 10, acts[i], Canvas::TextRole::Body, Gray::G0);
        }
        break;
      }
      if (sd_kind_ == SdContentKind::FirmwareRisk) {
        int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap, "This card looks modified.",
                                          Canvas::TextRole::Body, Gray::G0);
        y = canvas_.draw_text_wrapped(
            kSideMargin, y + 8, kWrapW, kLineGap,
            "Firmware or install files were found. Erase the card before linking.", Canvas::TextRole::Secondary,
            Gray::G1);
        focus_.count = 1;
        canvas_.draw_focus_tile(kSideMargin, y + 24, kCanvasW - 32, kFocusRowH, "Erase card", Canvas::TextRole::Body);
        break;
      }
      int body_y = ty + 52;
      if (sd_kind_ == SdContentKind::Media) {
        body_y = canvas_.draw_text_wrapped(kSideMargin, body_y, kWrapW, kLineGap, "Card has media files.",
                                           Canvas::TextRole::Body, Gray::G0);
        body_y = canvas_.draw_text_wrapped(kSideMargin, body_y + 8, kWrapW, kLineGap,
                                           "Erase to wipe, or eject to keep your music and photos.",
                                           Canvas::TextRole::Secondary, Gray::G1);
      } else if (sd_kind_ == SdContentKind::Empty) {
        body_y = canvas_.draw_text_wrapped(kSideMargin, body_y, kWrapW, kLineGap, "Empty card ready.",
                                           Canvas::TextRole::Body, Gray::G0);
        body_y = canvas_.draw_text_wrapped(kSideMargin, body_y + 8, kWrapW, kLineGap,
                                           "Erase to reformat, or continue.", Canvas::TextRole::Secondary, Gray::G1);
      } else {
        body_y = canvas_.draw_text_wrapped(kSideMargin, body_y, kWrapW, kLineGap, "Card detected.",
                                           Canvas::TextRole::Body, Gray::G0);
        body_y = canvas_.draw_text_wrapped(kSideMargin, body_y + 8, kWrapW, kLineGap, "Erase, eject, or continue.",
                                           Canvas::TextRole::Secondary, Gray::G1);
      }
      focus_.count = 3;
      const char* acts[] = {"Erase card", "Eject card", "Continue"};
      for (int i = 0; i < 3; ++i) {
        const int row_y = body_y + 24 + i * kRowPitch;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, row_y + 10, acts[i], Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingWifiList: {
      canvas_.draw_text(kSideMargin, ty, "Wi-Fi", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap, "Choose your network.",
                                        Canvas::TextRole::Secondary, Gray::G1);
      y = canvas_.draw_text_wrapped(kSideMargin, y + 4, kWrapW, kLineGap,
                                    "Enter the password in the Pocket app.", Canvas::TextRole::Secondary, Gray::G1);
      const int kListTop = y + 16;
      constexpr int kMaxVisible = 8;
      if (wifi_networks_.empty()) {
        canvas_.draw_text(kSideMargin, kListTop, "No networks found.", Canvas::TextRole::Body, Gray::G1);
      }
      const int shown = std::min(static_cast<int>(wifi_networks_.size()), kMaxVisible);
      focus_.count = shown + 1;
      for (int i = 0; i < shown; ++i) {
        const int row_y = kListTop + i * kRowPitch;
        if (i == focus_.index) {
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, wifi_networks_[i],
                                  Canvas::TextRole::Body);
        } else {
          canvas_.draw_text_fit(kSideMargin + 8, row_y + 10, kCanvasW - 48, wifi_networks_[i], Canvas::TextRole::Body,
                                Gray::G0);
        }
      }
      {
        const int row_y = wifi_networks_.empty() ? (kListTop + 40) : (kListTop + shown * kRowPitch);
        if (focus_.index == shown) {
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, "Rescan", Canvas::TextRole::Body);
        } else {
          canvas_.draw_text(kSideMargin + 8, row_y + 10, "Rescan", Canvas::TextRole::Body, Gray::G0);
        }
      }
      break;
    }
    case ScreenId::OnboardingWifiPassword: {
      if (wifi_ap_ssid_.empty()) wifi_ap_ssid_ = wifi_.provision_ap_ssid();
      if (wifi_ap_pass_.empty()) wifi_ap_pass_ = wifi_.provision_ap_password();

      canvas_.draw_text(kSideMargin, ty, "Phone setup", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 48, kWrapW, kLineGap,
                                        "Join Pocket's Wi-Fi, then enter your home network password.",
                                        Canvas::TextRole::Secondary, Gray::G1);

      canvas_.draw_text(kSideMargin, y + 16, "Join this network", Canvas::TextRole::Secondary, Gray::G1);
      if (!wifi_ap_ssid_.empty()) {
        canvas_.draw_text_fit(kSideMargin, y + 44, kWrapW, wifi_ap_ssid_, Canvas::TextRole::Body, Gray::G0);
      } else {
        canvas_.draw_text(kSideMargin, y + 44, "Could not start Wi-Fi", Canvas::TextRole::Body, Gray::G0);
      }

      canvas_.draw_text(kSideMargin, y + 88, "Password", Canvas::TextRole::Secondary, Gray::G1);
      if (!wifi_ap_pass_.empty()) {
        canvas_.draw_text(kSideMargin, y + 116, wifi_ap_pass_, Canvas::TextRole::ScreenTitle, Gray::G0);
      } else {
        canvas_.draw_text_wrapped(kSideMargin, y + 116, kWrapW, kLineGap, "No password — go back",
                                  Canvas::TextRole::Body, Gray::G0);
      }

      const int qr_size = 110;
      const int qr_x = (kCanvasW - qr_size) / 2;
      const int qr_y = std::max(y + 168, 360);
      if (!canvas_.draw_qr(qr_x, qr_y, qr_size, softap_portal_url())) {
        canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
      }
      canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 12, "Scan · or open 192.168.4.1",
                                 Canvas::TextRole::Secondary, Gray::G1);
      canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 44, "Waiting for home Wi-Fi…",
                                 Canvas::TextRole::Secondary, Gray::G1);

      const bool already_online = wifi_.connected();
      focus_.count = already_online ? 3 : 2;
      const char* actions_online[] = {"Waiting…", "Already online", "Cancel"};
      const char* actions_wait[] = {"Waiting…", "Cancel"};
      const char** actions = already_online ? actions_online : actions_wait;
      const int actions_top = std::min(qr_y + qr_size + 82, 640);
      for (int i = 0; i < focus_.count; ++i) {
        int row_y = actions_top + i * kRowPitch;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, actions[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, row_y + 10, actions[i], Canvas::TextRole::Body, Gray::G0);
      }
      if (now_ms_ < error_until_ms_) {
        canvas_.draw_text_wrapped(kSideMargin, kBottomCtaY, kWrapW, kLineGap, error_msg_, Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingWifiConnecting: {
      char buf[64];
      if (cfg_.wifi_ssid.empty()) {
        std::snprintf(buf, sizeof(buf), "Connecting…");
      } else {
        std::snprintf(buf, sizeof(buf), "Connecting to %s…", cfg_.wifi_ssid.c_str());
      }
      canvas_.draw_text(kSideMargin, ty, "Phone setup", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text_wrapped(kSideMargin, ty + 72, kWrapW, kLineGap, buf, Canvas::TextRole::Body, Gray::G0);
      break;
    }
    case ScreenId::OnboardingCompanionQr: {
      if (pair_code_.empty()) {
        if (!mint_pair_session()) {
          // Keep screen; Refresh will retry.
        }
      }

      canvas_.draw_text(kSideMargin, ty, "Link Pocket", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap,
                                        "Scan with the Pocket app to finish linking.", Canvas::TextRole::Secondary,
                                        Gray::G1);

      const int qr_size = 168;
      const int qr_x = (kCanvasW - qr_size) / 2;
      const int qr_y = std::max(y + 16, 200);
      if (!pair_code_.empty()) {
        const std::string url = companion_pair_url(pair_code_);
        if (!canvas_.draw_qr(qr_x, qr_y, qr_size, url)) {
          canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
          canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size / 2 - 4, "QR unavailable", Canvas::TextRole::Body,
                                     Gray::G1);
        }
      } else {
        canvas_.stroke_rect(qr_x, qr_y, qr_size, qr_size, Gray::G0);
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size / 2 - 4, "No code", Canvas::TextRole::Body, Gray::G1);
      }

      canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 20, "or go to", Canvas::TextRole::Secondary, Gray::G1);
      {
        const std::string site = companion_display_origin() + "/link";
        canvas_.draw_text_wrapped(kSideMargin, qr_y + qr_size + 52, kWrapW, kLineGap, site, Canvas::TextRole::Body,
                                  Gray::G0);
      }
      canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 100, pair_code_, Canvas::TextRole::WordMark, Gray::G0);
      if (pair_status_ == "expired") {
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 152, "Code expired — refresh below",
                                   Canvas::TextRole::Secondary, Gray::G0);
      } else if (pair_status_ == "claimed") {
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 152, "Linked", Canvas::TextRole::Secondary, Gray::G0);
      } else {
        canvas_.draw_text_centered(kCanvasW / 2, qr_y + qr_size + 152, "Expires in 10 minutes",
                                   Canvas::TextRole::Secondary, Gray::G1);
      }
      focus_.count = 2;
      const char* acts[] = {"Waiting for link…", "Refresh code"};
      const int actions_top = std::min(qr_y + qr_size + 200, 560);
      for (int i = 0; i < 2; ++i) {
        int row_y = actions_top + i * kRowPitch;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, row_y + 10, acts[i], Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingPinLength: {
      canvas_.draw_text(kSideMargin, ty, "Choose a PIN", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap,
                                        "Unlock Pocket with a short code. You can change it later in Settings.",
                                        Canvas::TextRole::Secondary, Gray::G1);
      focus_.count = 2;
      const char* opts[] = {"4 digits — quicker", "6 digits — stronger"};
      for (int i = 0; i < 2; ++i) {
        int row_y = y + 28 + i * kRowPitch;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, opts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, row_y + 10, opts[i], Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingPinSet:
    case ScreenId::OnboardingPinConfirm: {
      const bool confirm = s == ScreenId::OnboardingPinConfirm;
      canvas_.draw_text(kSideMargin, ty, confirm ? "Confirm PIN" : "Create PIN", Canvas::TextRole::ScreenTitle,
                        Gray::G0);
      if (confirm) {
        canvas_.draw_text_wrapped(kSideMargin, ty + 48, kWrapW, kLineGap, "Enter the same digits again.",
                                  Canvas::TextRole::Secondary, Gray::G1);
      } else {
        canvas_.draw_text_wrapped(kSideMargin, ty + 48, kWrapW, kLineGap, "Turn the dial, then press to lock each digit.",
                                  Canvas::TextRole::Secondary, Gray::G1);
      }
      // Show digits during setup (not masked) so the user can verify.
      draw_pin_entry(/*mask_completed=*/false, ty + 120);
      if (now_ms_ < error_until_ms_) {
        canvas_.draw_text_wrapped(kSideMargin, ty + 120 + 200, kWrapW, kLineGap, error_msg_, Canvas::TextRole::Body,
                                  Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingTimezone: {
      canvas_.draw_text(kSideMargin, ty, "Clock", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text(kSideMargin, ty + 52, "Time zone", Canvas::TextRole::Secondary, Gray::G1);
      focus_.count = kTzCount + 3;
      constexpr int kTzRowH = 44;
      const int kTzTop = ty + 88;
      for (int i = 0; i < kTzCount; ++i) {
        int row_y = kTzTop + i * kTzRowH;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kTzRowH - 4, kTimezones[i],
                                  Canvas::TextRole::Secondary);
        else
          canvas_.draw_text(kSideMargin + 8, row_y + 8, kTimezones[i], Canvas::TextRole::Secondary,
                            i == onboarding_tz_index_ ? Gray::G0 : Gray::G1);
      }
      int row_y = kTzTop + kTzCount * kTzRowH + 16;
      if (focus_.index == kTzCount)
        canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, "12-hour", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, row_y + 10, "12-hour", Canvas::TextRole::Body, Gray::G0);
      row_y += kRowPitch;
      if (focus_.index == kTzCount + 1)
        canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, "24-hour", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, row_y + 10, "24-hour", Canvas::TextRole::Body, Gray::G0);
      row_y += kRowPitch;
      if (focus_.index == kTzCount + 2)
        canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, "Continue", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, row_y + 10, "Continue", Canvas::TextRole::Body, Gray::G0);
      break;
    }
    case ScreenId::OnboardingMicTest: {
      canvas_.draw_text(kSideMargin, ty, "Voice", Canvas::TextRole::ScreenTitle, Gray::G0);
      int y = canvas_.draw_text_wrapped(kSideMargin, ty + 52, kWrapW, kLineGap, "Hold the side button and say something.",
                                        Canvas::TextRole::Body, Gray::G0);
      y = canvas_.draw_text_wrapped(kSideMargin, y + 8, kWrapW, kLineGap, "Speech recognition: Cloud",
                                    Canvas::TextRole::Secondary, Gray::G1);
      if (ptt_active_) {
        canvas_.draw_text(kSideMargin, y + 24, "Listening...", Canvas::TextRole::Body, Gray::G0);
      } else if (!mic_result_.empty()) {
        std::string line = "Heard: \"" + mic_result_ + "\"";
        canvas_.draw_text_wrapped(kSideMargin, y + 24, kWrapW, kLineGap, line, Canvas::TextRole::Body, Gray::G0);
      }
      focus_.count = 2;
      const char* acts[] = {"Try again", "Continue"};
      for (int i = 0; i < 2; ++i) {
        int row_y = 420 + i * kRowPitch;
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, row_y, kCanvasW - 32, kFocusRowH, acts[i], Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, row_y + 10, acts[i], Canvas::TextRole::Body, Gray::G0);
      }
      break;
    }
    case ScreenId::OnboardingDone: {
      canvas_.draw_text_centered(kCanvasW / 2, 200, "You're ready", Canvas::TextRole::ScreenTitle, Gray::G0);
      canvas_.draw_text_centered(kCanvasW / 2, 260, "Your phone is linked.", Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text_wrapped(kSideMargin, 296, kWrapW, kLineGap, "Manage name and Cloud in the app.",
                                Canvas::TextRole::Body, Gray::G0);
      canvas_.draw_text_centered(kCanvasW / 2, kBottomCtaY, "Press to go Home", Canvas::TextRole::Secondary, Gray::G1);
      break;
    }
    default:
      break;
  }
}

void App::handle_onboarding(InputEvent e) {
  ScreenId s = nav_.current();

  // PIN set/confirm: digit-level Back is handled in the PIN branch below.
  if (e == InputEvent::Back && s != ScreenId::OnboardingPinSet && s != ScreenId::OnboardingPinConfirm) {
    switch (s) {
      case ScreenId::OnboardingWelcome:
        break;
      case ScreenId::OnboardingWifiPassword:
        wifi_.stop_provision();
        if (cfg_.onboarding_complete) {
          nav_.replace(ScreenId::OnboardingWifiList);
        } else {
          nav_.replace(ScreenId::OnboardingSdCard);
        }
        after_nav();
        break;
      case ScreenId::OnboardingWifiList:
        if (cfg_.onboarding_complete) {
          nav_.replace(ScreenId::SettingsWifi);
        } else {
          nav_.replace(ScreenId::OnboardingSdCard);
        }
        after_nav();
        break;
      case ScreenId::OnboardingSdCard:
        sd_waiting_eject_ = false;
        nav_.replace(ScreenId::OnboardingCompanionDownload);
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
          // Re-enter Link SoftAP phase
          begin_softap_link();
          return;
        }
        after_nav();
        break;
      case ScreenId::OnboardingPinLength:
        nav_.replace(ScreenId::OnboardingCompanionQr);
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
    play_sound(SoundId::Click);
    focus_.index = 0;
    nav_.replace(ScreenId::OnboardingCompanionDownload);
    after_nav();
    return;
  }

  if (s == ScreenId::OnboardingCompanionDownload && e == InputEvent::Select) {
    play_sound(SoundId::Click);
    sd_waiting_eject_ = false;
    if (storage_) {
      storage_->probe();
      sd_kind_ = storage_->classify();
    } else {
      sd_kind_ = SdContentKind::Absent;
    }
    focus_.index = 0;
    nav_.replace(ScreenId::OnboardingSdCard);
    after_nav();
    return;
  }

  if (s == ScreenId::OnboardingSdCard) {
    if (sd_waiting_eject_) return;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
      return;
    }
    if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
      return;
    }
    if (e != InputEvent::Select) return;
    play_sound(SoundId::Click);

    auto refresh_sd = [&]() {
      if (storage_) {
        storage_->probe();
        sd_kind_ = storage_->classify();
      } else {
        sd_kind_ = SdContentKind::Absent;
      }
    };

    if (sd_kind_ == SdContentKind::Absent) {
      if (focus_.index == 0) {
        refresh_sd();
        mark_content_dirty();
      } else {
        begin_softap_link();
      }
      return;
    }
    if (sd_kind_ == SdContentKind::FirmwareRisk) {
      // Force erase only
      if (storage_ && storage_->erase_card()) {
        storage_->unmount();
        play_sound(SoundId::Success);
        begin_softap_link();
      } else {
        error_msg_ = "Couldn't erase the card.";
        error_until_ms_ = now_ms_ + 3000;
        play_sound(SoundId::Attention);
        mark_content_dirty();
      }
      return;
    }
    // Media / empty / unknown: Erase | Eject | Continue
    if (focus_.index == 0) {
      if (storage_ && storage_->erase_card()) {
        storage_->unmount();
        play_sound(SoundId::Success);
        begin_softap_link();
      } else {
        error_msg_ = "Couldn't erase the card.";
        error_until_ms_ = now_ms_ + 3000;
        play_sound(SoundId::Attention);
        mark_content_dirty();
      }
    } else if (focus_.index == 1) {
      if (storage_) storage_->unmount();
      sd_waiting_eject_ = true;
      last_sd_poll_ms_ = 0;
      play_sound(SoundId::Attention);
      mark_content_dirty();
    } else {
      begin_softap_link();
    }
    return;
  }

  if (s == ScreenId::OnboardingWifiList) {
    constexpr int kMaxVisible = 6;
    const int shown = std::min(static_cast<int>(wifi_networks_.size()), kMaxVisible);
    focus_.count = shown + 1;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index >= shown) {
        wifi_networks_ = wifi_.scan();
        mark_content_dirty();
      } else {
        cfg_.wifi_ssid = wifi_networks_[focus_.index];
        wifi_password_.clear();
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
        last_wifi_prov_poll_ms_ = 0;
        focus_.index = 0;
        nav_.replace(ScreenId::OnboardingWifiPassword);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingWifiPassword) {
    const bool already_online = wifi_.connected();
    focus_.count = already_online ? 3 : 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      const int cancel_i = already_online ? 2 : 1;
      const int skip_i = already_online ? 1 : -1;
      if (focus_.index == skip_i) {
        play_sound(SoundId::Click);
        wifi_.stop_provision();
        if (cfg_.onboarding_complete) {
          nav_.replace(ScreenId::SettingsWifi);
          after_nav();
        } else if (mint_pair_session()) {
          play_sound(SoundId::Success);
          focus_.index = 0;
          nav_.replace(ScreenId::OnboardingCompanionQr);
          after_nav();
        } else {
          play_sound(SoundId::Attention);
          mark_content_dirty();
        }
      } else if (focus_.index == cancel_i) {
        play_sound(SoundId::Click);
        wifi_.stop_provision();
        if (cfg_.onboarding_complete) {
          nav_.replace(ScreenId::OnboardingWifiList);
        } else {
          nav_.replace(ScreenId::OnboardingSdCard);
        }
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
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index == 1 || pair_status_ == "expired") {
        if (mint_pair_session()) {
          play_sound(SoundId::Click);
          mark_content_dirty();
        } else {
          play_sound(SoundId::Attention);
          mark_content_dirty();
        }
      }
      // index 0 = waiting — link arrives via tick poll
    }
    return;
  }

  if (s == ScreenId::OnboardingPinLength) {
    focus_.count = 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      cfg_.pin_length = focus_.index == 0 ? 4 : 6;
      pin_entry_.clear();
      pin_digit_working_ = '0';
      focus_.index = 0;
      nav_.replace(ScreenId::OnboardingPinSet);
      after_nav();
    }
    return;
  }

  if (s == ScreenId::OnboardingPinSet || s == ScreenId::OnboardingPinConfirm) {
    if (e == InputEvent::Up) {
      pin_digit_working_ = static_cast<char>('0' + ((pin_digit_working_ - '0' + 9) % 10));
      if (pin_entry_.size() <= static_cast<size_t>(focus_.index)) {
        if (pin_entry_.size() == static_cast<size_t>(focus_.index)) pin_entry_.push_back(pin_digit_working_);
      } else {
        pin_entry_[focus_.index] = pin_digit_working_;
      }
      mark_pin_dirty();
    } else if (e == InputEvent::Down) {
      pin_digit_working_ = static_cast<char>('0' + ((pin_digit_working_ - '0' + 1) % 10));
      if (pin_entry_.size() == static_cast<size_t>(focus_.index))
        pin_entry_.push_back(pin_digit_working_);
      else if (focus_.index < static_cast<int>(pin_entry_.size()))
        pin_entry_[focus_.index] = pin_digit_working_;
      mark_pin_dirty();
    } else if (e == InputEvent::Back) {
      if (pin_entry_.empty()) {
        if (s == ScreenId::OnboardingPinConfirm) {
          nav_.replace(ScreenId::OnboardingPinSet);
          pin_entry_.clear();
          pin_digit_working_ = '0';
          after_nav();
        } else {
          nav_.replace(ScreenId::OnboardingPinLength);
          after_nav();
        }
      } else {
        pin_entry_.pop_back();
        pin_digit_working_ = '0';
        focus_.index = static_cast<int>(pin_entry_.size());
        mark_pin_dirty();
      }
    } else if (e == InputEvent::Select) {
      if (pin_entry_.size() == static_cast<size_t>(focus_.index)) pin_entry_.push_back(pin_digit_working_);
      if (static_cast<int>(pin_entry_.size()) >= cfg_.pin_length) {
        if (s == ScreenId::OnboardingPinSet) {
          pin_pending_ = pin_entry_;
          pin_entry_.clear();
          focus_.index = 0;
          pin_digit_working_ = '0';
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
            error_msg_ = "Those didn't match. Try again.";
            error_until_ms_ = now_ms_ + 2500;
            pin_entry_.clear();
            focus_.index = 0;
            pin_digit_working_ = '0';
            nav_.replace(ScreenId::OnboardingPinSet);
            after_nav();
          }
        }
      } else {
        focus_.index = static_cast<int>(pin_entry_.size());
        pin_digit_working_ = '0';
        mark_pin_dirty();
      }
    }
    return;
  }

  if (s == ScreenId::OnboardingTimezone) {
    focus_.count = kTzCount + 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index < kTzCount) {
        onboarding_tz_index_ = focus_.index;
        cfg_.tz_id = kTimezones[focus_.index];
        mark_content_dirty();
      } else if (focus_.index == kTzCount) {
        cfg_.time_format = 12;
        mark_content_dirty();
      } else if (focus_.index == kTzCount + 1) {
        cfg_.time_format = 24;
        mark_content_dirty();
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
      mark_content_dirty();
    } else if (e == InputEvent::PttStop) {
      ptt_active_ = false;
      std::vector<uint8_t> fake;
      mic_result_ = cloud_.stt_transcribe(fake);
      if (mic_result_.empty()) mic_result_.clear();
      mark_content_dirty();
    } else {
      focus_.count = 2;
      if (e == InputEvent::Up) {
        focus_.move(-1);
        mark_content_dirty();
      } else if (e == InputEvent::Down) {
        focus_.move(1);
        mark_content_dirty();
      } else if (e == InputEvent::Select) {
        if (focus_.index == 0) {
          mic_result_.clear();
          mark_content_dirty();
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
    play_sound(SoundId::Success);
    cfg_.onboarding_complete = true;
    if (cfg_.device_name.empty()) cfg_.device_name = "Pocket";
    store_.save(cfg_);
    go_home();
  }
}

}  // namespace pocket
