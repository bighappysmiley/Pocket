#include "pocket/app.hpp"
#include <cstdio>
#include <string>

namespace pocket {

namespace {

constexpr int kTitleY = kContentTop;
constexpr int kListTop = kContentTop + 52;
constexpr int kRowTextPad = 10;

void draw_focus_rows(Canvas& c, FocusModel& focus, const char* const* rows, int count, int top_y) {
  focus.count = count;
  for (int i = 0; i < count; ++i) {
    const int y = top_y + i * kRowPitch;
    if (i == focus.index)
      c.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, rows[i], Canvas::TextRole::Body);
    else
      c.draw_text(kSideMargin + 8, y + kRowTextPad, rows[i], Canvas::TextRole::Body, Gray::G0);
  }
}

}  // namespace

static const char* kSettingsRows[] = {
    "PIN & security", "Wi-Fi",           "Display",         "Sound & mic", "Home apps",
    "Units",          "Pocket Cloud",    "Software update", "About",
};

void App::render_settings() {
  draw_status_bar();
  ScreenId s = nav_.current();

  if (s == ScreenId::SettingsRoot) {
    canvas_.draw_text(kSideMargin, kTitleY, "Settings", Canvas::TextRole::ScreenTitle, Gray::G0);
    draw_focus_rows(canvas_, focus_, kSettingsRows, 9, kListTop);
    return;
  }

  if (s == ScreenId::SettingsAbout) {
    canvas_.draw_text(kSideMargin, kTitleY, "About", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
    char ver[48];
    std::snprintf(ver, sizeof(ver), "Version %s", cfg_.fw_version.c_str());
    canvas_.draw_text(kSideMargin, kListTop + 52, ver, Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + 92, cfg_.device_name, Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + 132, "Pocket Display", Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(kSideMargin, kListTop + 180, "Pocket Cloud", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + 212, cfg_.cloud_entitled ? "Subscribed" : "Not subscribed",
                      Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(kSideMargin, kListTop + 252,
                      cfg_.companion_linked ? "Companion: Linked" : "Companion: Not linked",
                      Canvas::TextRole::Secondary, Gray::G1);
    const char* acts[] = {"Reset Pocket...", "Back"};
    draw_focus_rows(canvas_, focus_, acts, 2, 520);
    return;
  }

  if (s == ScreenId::SettingsCloud) {
    canvas_.draw_text(kSideMargin, kTitleY, "Pocket Cloud", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text_fit(kSideMargin, kListTop, kCanvasW - 32, "Sync Notes to your phone with Pocket Cloud.",
                          Canvas::TextRole::Secondary, Gray::G1);
    const char* status = "Not subscribed";
    if (cfg_.cloud_status == "trialing")
      status = "Trial";
    else if (cfg_.cloud_status == "active")
      status = "Subscribed";
    else if (cfg_.cloud_status == "past_due")
      status = "Payment issue";
    canvas_.draw_text(kSideMargin, kListTop + 48, status, Canvas::TextRole::Body, Gray::G0);
    if (cfg_.companion_linked) {
      canvas_.draw_text(kSideMargin, kListTop + 84, "Companion: Linked", Canvas::TextRole::Secondary, Gray::G1);
    } else {
      canvas_.draw_text(kSideMargin, kListTop + 84, "Companion: Not linked", Canvas::TextRole::Secondary, Gray::G1);
    }
    const char* acts[] = {"Start free trial", "Subscribe $3.99/mo", "Link companion app"};
    draw_focus_rows(canvas_, focus_, acts, 3, kListTop + 140);
    canvas_.draw_text(kSideMargin, kListTop + 140 + 3 * kRowPitch + 16, "Link opens a QR for the Pocket app.",
                      Canvas::TextRole::Secondary, Gray::G1);
    if (now_ms_ < error_until_ms_) {
      canvas_.draw_text_fit(kSideMargin, kListTop + 140 + 3 * kRowPitch + 52, kCanvasW - 32, error_msg_,
                            Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsDisplay) {
    canvas_.draw_text(kSideMargin, kTitleY, "Display", Canvas::TextRole::ScreenTitle, Gray::G0);
    char idle[48];
    std::snprintf(idle, sizeof(idle), "Idle lock: %ds", cfg_.idle_lock_s);
    const char* rows[] = {idle,
                          cfg_.show_batt_pct ? "Show battery %: On" : "Show battery %: Off", "Full refresh: Now",
                          "Ghosting control", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 5, kListTop);
    canvas_.draw_text(kSideMargin, kListTop + 5 * kRowPitch + 16,
                      "Pocket refreshes the screen to keep it clear.", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }

  if (s == ScreenId::SettingsSound) {
    canvas_.draw_text(kSideMargin, kTitleY, "Sound & mic", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop, "Speech recognition: Cloud", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + 40, "Hold BOOT in Notes.", Canvas::TextRole::Secondary, Gray::G1);
    const char* rows[] = {"Mic test", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 2, kListTop + 100);
    return;
  }

  if (s == ScreenId::SettingsUnits) {
    canvas_.draw_text(kSideMargin, kTitleY, "Units", Canvas::TextRole::ScreenTitle, Gray::G0);
    focus_.count = 2;
    const char* rows[] = {"Fahrenheit °F", "Celsius °C"};
    for (int i = 0; i < 2; ++i) {
      const int y = kListTop + i * kRowPitch;
      bool sel = (cfg_.weather_units == i);
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, rows[i], Canvas::TextRole::Body,
                          sel ? Gray::G0 : Gray::G1);
    }
    return;
  }

  if (s == ScreenId::SettingsHomeApps) {
    canvas_.draw_text(kSideMargin, kTitleY, "Home apps", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop - 8, "Choose what to show on Home.", Canvas::TextRole::Secondary,
                      Gray::G1);
    const char* names[] = {"Notes", "Ledger", "Clock", "Pass", "Weather", "Settings (required)"};
    focus_.count = 6;
    for (int i = 0; i < 6; ++i) {
      const int y = kListTop + 36 + i * kRowPitch;
      bool on = home_app_visible(cfg_, static_cast<HomeApp>(i));
      std::string label = std::string(names[i]) + (on ? ": On" : ": Off");
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, label, Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, label, Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsUpdate) {
    canvas_.draw_text(kSideMargin, kTitleY, "Software update", Canvas::TextRole::ScreenTitle, Gray::G0);
    char ver[48];
    std::snprintf(ver, sizeof(ver), "Version %s", cfg_.fw_version.c_str());
    canvas_.draw_text(kSideMargin, kListTop, ver, Canvas::TextRole::Body, Gray::G0);
    const char* rows[] = {"Check for update", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 2, kListTop + 72);
    canvas_.draw_text(kSideMargin, kListTop + 72 + 2 * kRowPitch + 16, "You're up to date",
                      Canvas::TextRole::Secondary, Gray::G1);
    return;
  }

  if (s == ScreenId::SettingsWifi) {
    canvas_.draw_text(kSideMargin, kTitleY, "Wi-Fi", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop, wifi_.connected() ? cfg_.wifi_ssid : "Not connected",
                      Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + 40, "Password is entered on your phone.",
                      Canvas::TextRole::Secondary, Gray::G1);
    const char* rows[] = {"Set up with phone…", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 2, kListTop + 100);
    return;
  }

  if (s == ScreenId::SettingsSecurity) {
    canvas_.draw_text(kSideMargin, kTitleY, "PIN & security", Canvas::TextRole::ScreenTitle, Gray::G0);
    const char* rows[] = {"Change PIN", "Lock now", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 3, kListTop);
    return;
  }

  // Fallback
  canvas_.draw_text(kSideMargin, kTitleY, "Settings", Canvas::TextRole::ScreenTitle, Gray::G0);
}

void App::handle_settings(InputEvent e) {
  ScreenId s = nav_.current();
  if (e == InputEvent::Back) {
    if (s == ScreenId::SettingsRoot) {
      nav_.pop();
      after_nav();
    } else {
      nav_.replace(ScreenId::SettingsRoot);
      after_nav();
    }
    return;
  }

  if (s == ScreenId::SettingsRoot) {
    focus_.count = 9;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      ScreenId dest[] = {ScreenId::SettingsSecurity, ScreenId::SettingsWifi,     ScreenId::SettingsDisplay,
                         ScreenId::SettingsSound,    ScreenId::SettingsHomeApps, ScreenId::SettingsUnits,
                         ScreenId::SettingsCloud,    ScreenId::SettingsUpdate,   ScreenId::SettingsAbout};
      nav_.push(dest[focus_.index]);
      after_nav();
    }
    return;
  }

  if (s == ScreenId::SettingsUnits && (e == InputEvent::Select || e == InputEvent::Up || e == InputEvent::Down)) {
    focus_.count = 2;
    if (e == InputEvent::Up || e == InputEvent::Down) {
      focus_.move(e == InputEvent::Down ? 1 : -1);
      dirty_ = true;
    } else {
      cfg_.weather_units = static_cast<uint8_t>(focus_.index);
      store_.save(cfg_);
      dirty_ = true;
    }
    return;
  }

  if (s == ScreenId::SettingsHomeApps) {
    focus_.count = 6;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index != 5) {
        cfg_.home_visible ^= static_cast<uint8_t>(1u << focus_.index);
        store_.save(cfg_);
        dirty_ = true;
      }
    }
    return;
  }

  if (s == ScreenId::SettingsSecurity) {
    focus_.count = 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 1) go_lock();
      else if (focus_.index == 2) {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsWifi) {
    focus_.count = 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        wifi_networks_ = wifi_.scan();
        cfg_.wifi_ssid.clear();
        focus_.index = 0;
        nav_.push(ScreenId::OnboardingWifiList);
        after_nav();
      } else {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsDisplay) {
    focus_.count = 5;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        const uint16_t opts[] = {30, 60, 120, 300};
        int cur = 0;
        for (int i = 0; i < 4; ++i)
          if (opts[i] == cfg_.idle_lock_s) cur = i;
        cfg_.idle_lock_s = opts[(cur + 1) % 4];
        store_.save(cfg_);
        dirty_ = true;
      } else if (focus_.index == 1) {
        cfg_.show_batt_pct = !cfg_.show_batt_pct;
        store_.save(cfg_);
        dirty_ = true;
      } else if (focus_.index == 2) {
        redraw(true);
      } else if (focus_.index == 4) {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsAbout) {
    focus_.count = 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        // Reset confirm — immediate wipe for v1 sim
        DeviceConfig fresh;
        fresh.device_id = cfg_.device_id;
        cfg_ = fresh;
        store_.save(cfg_);
        nav_.reset(ScreenId::OnboardingWelcome);
        after_nav();
      } else {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsCloud) {
    focus_.count = 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      dirty_ = true;
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      dirty_ = true;
    } else if (e == InputEvent::Select) {
      if (focus_.index == 2) {
        // Link companion app — mint pair session + show QR
        if (!wifi_.connected()) {
          error_msg_ = "Connect to Wi-Fi first.";
          error_until_ms_ = now_ms_ + 2500;
          dirty_ = true;
        } else {
          pair_code_ = cloud_.create_pair_session(cfg_.device_id);
          pair_expires_ms_ = now_ms_ + 10 * 60 * 1000;
          pair_status_ = "pending";
          last_pair_poll_ms_ = 0;
          focus_.index = 0;
          nav_.push(ScreenId::OnboardingCompanionQr);
          after_nav();
        }
      } else {
        // Trial / subscribe — open companion billing via same pair path if unlinked
        dirty_ = true;
      }
    }
    return;
  }

  // Generic: Up/Down focus, Select/Back
  if (e == InputEvent::Up) {
    focus_.move(-1);
    dirty_ = true;
  } else if (e == InputEvent::Down) {
    focus_.move(1);
    dirty_ = true;
  } else if (e == InputEvent::Select) {
    nav_.replace(ScreenId::SettingsRoot);
    after_nav();
  }
}

}  // namespace pocket
