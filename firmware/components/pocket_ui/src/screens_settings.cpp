#include "pocket/app.hpp"
#include <cstdio>
#include <string>

namespace pocket {

static const char* kSettingsRows[] = {
    "PIN & security", "Wi-Fi",           "Display",         "Sound & mic", "Home apps",
    "Units",          "Pocket Cloud",    "Software update", "About",
};

void App::render_settings() {
  draw_status_bar();
  ScreenId s = nav_.current();

  if (s == ScreenId::SettingsRoot) {
    canvas_.draw_text(16, 40, "Settings", Canvas::TextRole::ScreenTitle, Gray::G0);
    focus_.count = 9;
    for (int i = 0; i < 9; ++i) {
      int y = 90 + i * 52;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, kSettingsRows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, kSettingsRows[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsAbout) {
    canvas_.draw_text(16, 40, "About", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(16, 100, "Pocket", Canvas::TextRole::WordMark, Gray::G0);
    char ver[48];
    std::snprintf(ver, sizeof(ver), "Version %s", cfg_.fw_version.c_str());
    canvas_.draw_text(16, 150, ver, Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(16, 190, cfg_.device_name, Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(16, 230, "Pocket Display", Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(16, 280, "Pocket Cloud", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(16, 310, cfg_.cloud_entitled ? "Subscribed" : "Not subscribed", Canvas::TextRole::Secondary,
                      Gray::G1);
    canvas_.draw_text(16, 360, cfg_.companion_linked ? "Companion: Linked" : "Companion: Not linked",
                      Canvas::TextRole::Secondary, Gray::G1);
    focus_.count = 2;
    const char* acts[] = {"Reset Pocket…", "Back"};
    for (int i = 0; i < 2; ++i) {
      int y = 500 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, acts[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, acts[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsCloud) {
    canvas_.draw_text(16, 40, "Pocket Cloud", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(16, 90, "Sync Notes to your phone with Pocket Cloud.", Canvas::TextRole::Secondary, Gray::G1);
    const char* status = "Not subscribed";
    if (cfg_.cloud_status == "trialing")
      status = "Trial";
    else if (cfg_.cloud_status == "active")
      status = "Subscribed";
    else if (cfg_.cloud_status == "past_due")
      status = "Payment issue";
    canvas_.draw_text(16, 140, status, Canvas::TextRole::Body, Gray::G0);
    focus_.count = 3;
    const char* acts[] = {"Start free trial", "Subscribe — $3.99/mo", "Link account"};
    for (int i = 0; i < 3; ++i) {
      int y = 220 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, acts[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, acts[i], Canvas::TextRole::Body, Gray::G0);
    }
    // QR sheet hint
    canvas_.draw_text(16, 420, "Continue on your phone", Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(16, 450, "Scan with your phone camera", Canvas::TextRole::Secondary, Gray::G1);
    canvas_.stroke_rect(140, 500, 200, 200, Gray::G0);
    return;
  }

  if (s == ScreenId::SettingsDisplay) {
    canvas_.draw_text(16, 40, "Display", Canvas::TextRole::ScreenTitle, Gray::G0);
    focus_.count = 5;
    char idle[48];
    std::snprintf(idle, sizeof(idle), "Idle lock · %ds", cfg_.idle_lock_s);
    const char* rows[] = {idle,
                          cfg_.show_batt_pct ? "Show battery % · On" : "Show battery % · Off", "Full refresh · Now",
                          "Ghosting control", "Back"};
    for (int i = 0; i < 5; ++i) {
      int y = 100 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, rows[i], Canvas::TextRole::Body, Gray::G0);
    }
    canvas_.draw_text(16, 420, "Pocket refreshes the screen to keep it clear.", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }

  if (s == ScreenId::SettingsSound) {
    canvas_.draw_text(16, 40, "Sound & mic", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(16, 100, "Speech recognition: Cloud", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(16, 140, "Hold BOOT in Notes.", Canvas::TextRole::Secondary, Gray::G1);
    focus_.count = 2;
    const char* rows[] = {"Mic test", "Back"};
    for (int i = 0; i < 2; ++i) {
      int y = 200 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, rows[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsUnits) {
    canvas_.draw_text(16, 40, "Units", Canvas::TextRole::ScreenTitle, Gray::G0);
    focus_.count = 2;
    const char* rows[] = {"Fahrenheit °F", "Celsius °C"};
    for (int i = 0; i < 2; ++i) {
      int y = 120 + i * 56;
      bool sel = (cfg_.weather_units == i);
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, rows[i], Canvas::TextRole::Body, sel ? Gray::G0 : Gray::G1);
    }
    return;
  }

  if (s == ScreenId::SettingsHomeApps) {
    canvas_.draw_text(16, 40, "Home apps", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(16, 80, "Choose what to show on Home.", Canvas::TextRole::Secondary, Gray::G1);
    const char* names[] = {"Notes", "Ledger", "Clock", "Pass", "Weather", "Settings (required)"};
    focus_.count = 6;
    for (int i = 0; i < 6; ++i) {
      int y = 120 + i * 52;
      bool on = home_app_visible(cfg_, static_cast<HomeApp>(i));
      std::string label = std::string(names[i]) + (on ? " · On" : " · Off");
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, label, Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, label, Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsUpdate) {
    canvas_.draw_text(16, 40, "Software update", Canvas::TextRole::ScreenTitle, Gray::G0);
    char ver[48];
    std::snprintf(ver, sizeof(ver), "Version %s", cfg_.fw_version.c_str());
    canvas_.draw_text(16, 100, ver, Canvas::TextRole::Body, Gray::G0);
    focus_.count = 2;
    const char* rows[] = {"Check for update", "Back"};
    for (int i = 0; i < 2; ++i) {
      int y = 180 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, rows[i], Canvas::TextRole::Body, Gray::G0);
    }
    canvas_.draw_text(16, 320, "You're up to date", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }

  if (s == ScreenId::SettingsWifi) {
    canvas_.draw_text(16, 40, "Wi-Fi", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(16, 100, wifi_.connected() ? cfg_.wifi_ssid : "Not connected", Canvas::TextRole::Body, Gray::G0);
    focus_.count = 2;
    const char* rows[] = {"Choose network…", "Back"};
    for (int i = 0; i < 2; ++i) {
      int y = 180 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, rows[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsSecurity) {
    canvas_.draw_text(16, 40, "PIN & security", Canvas::TextRole::ScreenTitle, Gray::G0);
    focus_.count = 3;
    const char* rows[] = {"Change PIN", "Lock now", "Back"};
    for (int i = 0; i < 3; ++i) {
      int y = 120 + i * 56;
      if (i == focus_.index)
        canvas_.draw_focus_tile(16, y, kCanvasW - 32, 48, rows[i], Canvas::TextRole::Body);
      else
        canvas_.draw_text(24, y + 14, rows[i], Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  // Fallback
  canvas_.draw_text(16, 40, "Settings", Canvas::TextRole::ScreenTitle, Gray::G0);
}

void App::handle_settings(InputEvent e) {
  ScreenId s = nav_.current();
  if (e == InputEvent::Back) {
    if (s == ScreenId::SettingsRoot) {
      nav_.pop();
      after_nav(true);
    } else {
      nav_.replace(ScreenId::SettingsRoot);
      after_nav(true);
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
      after_nav(true);
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
        after_nav(true);
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
        after_nav(true);
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
        after_nav(true);
      } else {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav(true);
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
    after_nav(true);
  }
}

}  // namespace pocket