#include "pocket/app.hpp"
#include <cstdio>
#include <string>

namespace pocket {

namespace {

constexpr int kTitleY = kContentTop;
constexpr int kListTop = kContentTop + kTitleToBody;
constexpr int kRowTextPad = 12;

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
    canvas_.draw_pocket_wordmark(kSideMargin, kListTop, Gray::G0);
    char ver[48];
    std::snprintf(ver, sizeof(ver), "Version %s", kConsumerVersion);
    canvas_.draw_text(kSideMargin, kListTop + kTitleToBody, ver, Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + kTitleToBody + kBodyLinePitch, cfg_.device_name,
                      Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(kSideMargin, kListTop + kTitleToBody + 2 * kBodyLinePitch + 8, "What's new",
                      Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text_wrapped(
        kSideMargin, kListTop + kTitleToBody + 3 * kBodyLinePitch + 8, kContentW, 5,
        "Calmer lock screen, quieter icons, optional lock message.",
        Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(kSideMargin, kListTop + kTitleToBody + 6 * kBodyLinePitch + 16, "Pocket Cloud",
                      Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + kTitleToBody + 7 * kBodyLinePitch + 16,
                      cfg_.cloud_entitled ? "Subscribed" : "Not subscribed", Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text(kSideMargin, kListTop + kTitleToBody + 8 * kBodyLinePitch + 16,
                      cfg_.companion_linked ? "Companion: Linked" : "Companion: Not linked",
                      Canvas::TextRole::Secondary, Gray::G1);
    const char* acts[] = {"Controls tips", "Reset Pocket...", "Back"};
    draw_focus_rows(canvas_, focus_, acts, 3, 560);
    return;
  }

  if (s == ScreenId::SettingsCloud) {
    canvas_.draw_text(kSideMargin, kTitleY, "Pocket Cloud", Canvas::TextRole::ScreenTitle, Gray::G0);
    int sync_y = canvas_.draw_text_wrapped(kSideMargin, kListTop, kContentW, 6,
                                           "Sync Notes to your phone with Pocket Cloud.",
                                           Canvas::TextRole::Secondary, Gray::G1);
    const char* status = "Not subscribed";
    if (cfg_.cloud_status == "trialing")
      status = "Trial";
    else if (cfg_.cloud_status == "active")
      status = "Subscribed";
    else if (cfg_.cloud_status == "past_due")
      status = "Payment issue";
    canvas_.draw_text(kSideMargin, sync_y + 16, status, Canvas::TextRole::Body, Gray::G0);
    if (cfg_.companion_linked) {
      canvas_.draw_text(kSideMargin, sync_y + 52, "Companion: Linked", Canvas::TextRole::Secondary, Gray::G1);
    } else {
      canvas_.draw_text(kSideMargin, sync_y + 52, "Companion: Not linked", Canvas::TextRole::Secondary, Gray::G1);
    }
    const char* acts[] = {"Start free trial", "Subscribe $3.99/mo", "Link companion app"};
    draw_focus_rows(canvas_, focus_, acts, 3, sync_y + 100);
    canvas_.draw_text_wrapped(kSideMargin, sync_y + 100 + 3 * kRowPitch + 16, kContentW, 6,
                              "Link opens a QR for the Pocket app.", Canvas::TextRole::Secondary, Gray::G1);
    if (now_ms_ < error_until_ms_) {
      canvas_.draw_text_wrapped(kSideMargin, sync_y + 100 + 3 * kRowPitch + 52, kContentW, 6, error_msg_,
                                Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsDisplay) {
    canvas_.draw_text(kSideMargin, kTitleY, "Display", Canvas::TextRole::ScreenTitle, Gray::G0);
    char idle[48];
    std::snprintf(idle, sizeof(idle), "Idle lock: %ds", cfg_.idle_lock_s);
    std::string lockmsg =
        cfg_.lock_message.empty() ? "Lock message: Off" : "Lock message: " + cfg_.lock_message;
    const char* rows[] = {idle,
                          cfg_.show_batt_pct ? "Show battery %: On" : "Show battery %: Off",
                          lockmsg.c_str(), "Full refresh: Now", "Ghosting control", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 6, kListTop);
    canvas_.draw_text_wrapped(kSideMargin, kListTop + 6 * kRowPitch + 16, kContentW, 6,
                              "Set a custom lock message in Pocket Companion.",
                              Canvas::TextRole::Secondary, Gray::G1);
    return;
  }

  if (s == ScreenId::SettingsSound) {
    canvas_.draw_text(kSideMargin, kTitleY, "Sound & mic", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop, "Speech recognition: Cloud", Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop + 40, "Hold the side button in Notes.", Canvas::TextRole::Secondary, Gray::G1);
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
    // Settings + Update always visible on Home; not toggleable here.
    const char* names[] = {"Notes", "Ledger", "Clock", "Pass", "Weather", "Music",
                           "Settings (always on)", "Update (always on)"};
    focus_.count = 8;
    for (int i = 0; i < 8; ++i) {
      const int y = kListTop + 28 + i * kRowPitch;
      bool on = home_app_visible(cfg_, static_cast<HomeApp>(i));
      std::string label = std::string(names[i]);
      if (i < 6) label += on ? ": On" : ": Off";
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, label, Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, label, Canvas::TextRole::Body, Gray::G0);
    }
    return;
  }

  if (s == ScreenId::SettingsUpdate) {
    canvas_.draw_text(kSideMargin, kTitleY, "Software update", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text(kSideMargin, kListTop, kProductName, Canvas::TextRole::Body, Gray::G0);
    char ver[48];
    std::snprintf(ver, sizeof(ver), "Version %s", kConsumerVersion);
    canvas_.draw_text(kSideMargin, kListTop + kBodyLinePitch, ver, Canvas::TextRole::Secondary, Gray::G1);
    canvas_.draw_text_wrapped(kSideMargin, kListTop + 2 * kBodyLinePitch, kContentW, 4,
                              "Check Pocket Cloud for the latest software.",
                              Canvas::TextRole::Secondary, Gray::G1);
    const char* rows[] = {"Update Pocket", "Back"};
    draw_focus_rows(canvas_, focus_, rows, 2, kListTop + 72 + 2 * kBodyLinePitch);
    return;
  }

  if (s == ScreenId::SettingsUpdateProgress) {
    canvas_.draw_text(kSideMargin, kTitleY, "Updating", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, kListTop, kContentW, 6,
                              ota_status_.empty() ? "Working…" : ota_status_, Canvas::TextRole::Body, Gray::G0);
    canvas_.draw_text(kSideMargin, kBottomCtaY, "Keep Pocket plugged in", Canvas::TextRole::Secondary, Gray::G1);
    return;
  }

  if (s == ScreenId::SettingsUpdateResult) {
    canvas_.draw_text(kSideMargin, kTitleY, "Update", Canvas::TextRole::ScreenTitle, Gray::G0);
    canvas_.draw_text_wrapped(kSideMargin, kListTop, kContentW, 6,
                              ota_status_.empty() ? "Done." : ota_status_, Canvas::TextRole::Body, Gray::G0);
    focus_.count = 1;
    canvas_.draw_focus_tile(kSideMargin, kBottomCtaY, kCanvasW - 32, kFocusRowH, "OK", Canvas::TextRole::Body);
    return;
  }

  if (s == ScreenId::SettingsWifi) {
    canvas_.draw_text(kSideMargin, kTitleY, "Wi-Fi", Canvas::TextRole::ScreenTitle, Gray::G0);

    if (wifi_ui_page_ == 1) {
      // Add network — finish in Companion (hotspot or another Wi‑Fi).
      canvas_.draw_text_wrapped(kSideMargin, kListTop, kContentW, 5,
                                "Add a network in Pocket Companion. Choose Personal Hotspot or another Wi‑Fi.",
                                Canvas::TextRole::Body, Gray::G0);
      const bool online = wifi_.connected();
      if (online) {
        canvas_.draw_text_wrapped(kSideMargin, kListTop + 120, kContentW, 4,
                                  "Phone: Devices → this Pocket → Add Wi‑Fi. Pocket stays online — no setup hop.",
                                  Canvas::TextRole::Secondary, Gray::G1);
      } else {
        canvas_.draw_text_wrapped(kSideMargin, kListTop + 120, kContentW, 5,
                                  "Pocket will open a short setup network so Companion can send the password.",
                                  Canvas::TextRole::Secondary, Gray::G1);
      }
      const char* acts[] = {"Personal Hotspot", "Another Wi‑Fi", "Back"};
      focus_.count = 3;
      draw_focus_rows(canvas_, focus_, acts, 3, kListTop + 240);
      return;
    }

    if (wifi_ui_page_ == 2) {
      canvas_.draw_text(kSideMargin, kListTop, "Remove a saved network", Canvas::TextRole::Body, Gray::G0);
      const int n_known = static_cast<int>(cfg_.wifi_known.size());
      focus_.count = std::max(1, n_known + 1);
      int y = kListTop + 56;
      for (int i = 0; i < n_known; ++i) {
        const auto& net = cfg_.wifi_known[static_cast<size_t>(i)];
        if (i == focus_.index)
          canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, net.ssid, Canvas::TextRole::Body);
        else
          canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, net.ssid.c_str(), Canvas::TextRole::Body, Gray::G0);
        y += kRowPitch;
      }
      if (focus_.index == n_known)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, "Back", Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, "Back", Canvas::TextRole::Body, Gray::G0);
      return;
    }

    // Page 0 — known / connected list.
    const bool online = wifi_.connected();
    char status[96];
    if (online) {
      std::snprintf(status, sizeof(status), "Connected · %s", cfg_.wifi_ssid.c_str());
    } else if (wifi_sta_connecting_) {
      std::snprintf(status, sizeof(status), "Connecting…");
    } else {
      std::snprintf(status, sizeof(status), "Not connected");
    }
    canvas_.draw_text_fit(kSideMargin, kListTop, kContentW, status, Canvas::TextRole::Body, Gray::G0);

    const int n_known = static_cast<int>(cfg_.wifi_known.size());
    const int n_actions = n_known > 0 ? 3 : 2;  // Add · [Remove] · Back
    focus_.count = std::max(1, n_known + n_actions);
    int y = kListTop + 56;
    if (n_known == 0) {
      canvas_.draw_text(kSideMargin, y, "No saved networks yet", Canvas::TextRole::Secondary, Gray::G1);
      y += kRowPitch;
    }
    for (int i = 0; i < n_known; ++i) {
      const auto& net = cfg_.wifi_known[static_cast<size_t>(i)];
      std::string label = net.ssid;
      if (online && net.ssid == cfg_.wifi_ssid) label += " · now";
      if (i == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, label, Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, label.c_str(), Canvas::TextRole::Body, Gray::G0);
      y += kRowPitch;
    }
    const char* add_label = "Add network…";
    const char* rem_label = "Remove network…";
    int a0 = n_known;
    if (a0 == focus_.index)
      canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, add_label, Canvas::TextRole::Body);
    else
      canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, add_label, Canvas::TextRole::Body, Gray::G0);
    y += kRowPitch;
    if (n_known > 0) {
      if (a0 + 1 == focus_.index)
        canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, rem_label, Canvas::TextRole::Body);
      else
        canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, rem_label, Canvas::TextRole::Body, Gray::G0);
      y += kRowPitch;
    }
    const int back_i = n_known + n_actions - 1;
    if (back_i == focus_.index)
      canvas_.draw_focus_tile(kSideMargin, y, kCanvasW - 32, kFocusRowH, "Back", Canvas::TextRole::Body);
    else
      canvas_.draw_text(kSideMargin + 8, y + kRowTextPad, "Back", Canvas::TextRole::Body, Gray::G0);

    if (now_ms_ < error_until_ms_ && !error_msg_.empty()) {
      canvas_.draw_text_wrapped(kSideMargin, y + kRowPitch + 8, kContentW, 4, error_msg_,
                                Canvas::TextRole::Secondary, Gray::G0);
    }
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
    if (s == ScreenId::SettingsWifi && wifi_ui_page_ != 0) {
      wifi_ui_page_ = 0;
      focus_.index = 0;
      mark_content_dirty();
      return;
    }
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
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      ScreenId dest[] = {ScreenId::SettingsSecurity, ScreenId::SettingsWifi,     ScreenId::SettingsDisplay,
                         ScreenId::SettingsSound,    ScreenId::SettingsHomeApps, ScreenId::SettingsUnits,
                         ScreenId::SettingsCloud,    ScreenId::SettingsUpdate,   ScreenId::SettingsAbout};
      if (dest[focus_.index] == ScreenId::SettingsWifi) wifi_ui_page_ = 0;
      nav_.push(dest[focus_.index]);
      after_nav();
    }
    return;
  }

  if (s == ScreenId::SettingsUnits && (e == InputEvent::Select || e == InputEvent::Up || e == InputEvent::Down)) {
    focus_.count = 2;
    if (e == InputEvent::Up || e == InputEvent::Down) {
      focus_.move(e == InputEvent::Down ? 1 : -1);
      mark_content_dirty();
    } else {
      cfg_.weather_units = static_cast<uint8_t>(focus_.index);
      store_.save(cfg_);
      mark_content_dirty();
    }
    return;
  }

  if (s == ScreenId::SettingsHomeApps) {
    focus_.count = 8;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      // Settings (6) + Update (7) always on
      if (focus_.index < 6) {
        cfg_.home_visible ^= static_cast<uint16_t>(1u << focus_.index);
        store_.save(cfg_);
        mark_content_dirty();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsSecurity) {
    focus_.count = 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
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
    const int n_known = static_cast<int>(cfg_.wifi_known.size());

    if (wifi_ui_page_ == 1) {
      focus_.count = 3;
      if (e == InputEvent::Up || e == InputEvent::Down) {
        focus_.move(e == InputEvent::Down ? 1 : -1);
        mark_content_dirty();
        return;
      }
      if (e == InputEvent::Select) {
        if (focus_.index == 2) {
          wifi_ui_page_ = 0;
          focus_.index = n_known;  // Add row
          mark_content_dirty();
          return;
        }
        // Hotspot (0) or Another Wi‑Fi (1): finish in Companion.
        play_sound(SoundId::Click);
        if (wifi_.connected()) {
          error_msg_ = focus_.index == 0
                           ? "In Companion: Devices → Add Wi‑Fi → Use phone data."
                           : "In Companion: Devices → Add Wi‑Fi → Home Wi‑Fi.";
          error_until_ms_ = now_ms_ + 5000;
          wifi_ui_page_ = 0;
          focus_.index = 0;
          mark_content_dirty();
        } else {
          // Offline: SoftAP so Companion Link can hand off credentials.
          wifi_ui_page_ = 0;
          begin_add_wifi_network();
        }
      }
      return;
    }

    if (wifi_ui_page_ == 2) {
      focus_.count = std::max(1, n_known + 1);
      if (e == InputEvent::Up || e == InputEvent::Down) {
        focus_.move(e == InputEvent::Down ? 1 : -1);
        mark_content_dirty();
        return;
      }
      if (e == InputEvent::Select) {
        if (focus_.index >= n_known) {
          wifi_ui_page_ = 0;
          focus_.index = 0;
          mark_content_dirty();
          return;
        }
        const std::string victim = cfg_.wifi_known[static_cast<size_t>(focus_.index)].ssid;
        wifi_known_forget(cfg_, victim);
        store_.save(cfg_);
        play_sound(SoundId::Click);
        if (cfg_.wifi_known.empty()) {
          wifi_ui_page_ = 0;
          focus_.index = 0;
        } else if (focus_.index >= static_cast<int>(cfg_.wifi_known.size())) {
          focus_.index = static_cast<int>(cfg_.wifi_known.size()) - 1;
        }
        mark_content_dirty();
      }
      return;
    }

    const int n_actions = n_known > 0 ? 3 : 2;
    focus_.count = std::max(1, n_known + n_actions);
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index < n_known) {
        const auto& net = cfg_.wifi_known[static_cast<size_t>(focus_.index)];
        play_sound(SoundId::Click);
        error_msg_ = "Connecting…";
        error_until_ms_ = now_ms_ + 2000;
        mark_content_dirty();
        wifi_sta_connecting_ = true;
        const bool ok = connect_and_remember(net.ssid, net.password);
        wifi_sta_connecting_ = false;
        if (ok) {
          play_sound(SoundId::Success);
          error_msg_.clear();
        } else {
          play_sound(SoundId::Attention);
          error_msg_ = "Couldn't join that network.";
          error_until_ms_ = now_ms_ + 3000;
        }
        mark_content_dirty();
      } else if (focus_.index == n_known) {
        // Add network…
        wifi_ui_page_ = 1;
        focus_.index = 0;
        mark_content_dirty();
      } else if (n_known > 0 && focus_.index == n_known + 1) {
        wifi_ui_page_ = 2;
        focus_.index = 0;
        mark_content_dirty();
      } else {
        wifi_ui_page_ = 0;
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsDisplay) {
    focus_.count = 6;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        const uint16_t opts[] = {30, 60, 120, 300};
        int cur = 0;
        for (int i = 0; i < 4; ++i)
          if (opts[i] == cfg_.idle_lock_s) cur = i;
        cfg_.idle_lock_s = opts[(cur + 1) % 4];
        store_.save(cfg_);
        mark_content_dirty();
      } else if (focus_.index == 1) {
        cfg_.show_batt_pct = !cfg_.show_batt_pct;
        store_.save(cfg_);
        mark_content_dirty();
      } else if (focus_.index == 2) {
        // Quick on-device toggle: Off ↔ device name. Custom text: Companion.
        cfg_.lock_message = cfg_.lock_message.empty() ? cfg_.device_name : std::string();
        store_.save(cfg_);
        mark_content_dirty();
      } else if (focus_.index == 3) {
        redraw(true);
      } else if (focus_.index == 5) {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsAbout) {
    focus_.count = 3;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) {
        tips_page_ = 1;
        cfg_.seen_whats_new_build = cfg_.fw_build_id;
        store_.save(cfg_);
        nav_.push(ScreenId::OnboardingDone);
        after_nav();
      } else if (focus_.index == 1) {
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
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index == 2) {
        // Link companion app — mint pair session + show QR
        if (!wifi_.connected()) {
          error_msg_ = "Connect to Wi-Fi first.";
          error_until_ms_ = now_ms_ + 2500;
          mark_content_dirty();
        } else {
          if (!mint_pair_session()) {
            error_msg_ = "Couldn't create pairing code.";
            error_until_ms_ = now_ms_ + 2500;
            mark_content_dirty();
            return;
          }
          focus_.index = 0;
          nav_.push(ScreenId::OnboardingCompanionQr);
          after_nav();
        }
      } else {
        // Trial / subscribe — open companion billing via same pair path if unlinked
        mark_content_dirty();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsUpdate) {
    focus_.count = 2;
    if (e == InputEvent::Up) {
      focus_.move(-1);
      mark_content_dirty();
    } else if (e == InputEvent::Down) {
      focus_.move(1);
      mark_content_dirty();
    } else if (e == InputEvent::Select) {
      if (focus_.index == 0) begin_firmware_update();
      else {
        nav_.replace(ScreenId::SettingsRoot);
        after_nav();
      }
    }
    return;
  }

  if (s == ScreenId::SettingsUpdateProgress) {
    return;  // blocking update owns the screen
  }

  if (s == ScreenId::SettingsUpdateResult) {
    if (e == InputEvent::Select || e == InputEvent::Back) {
      nav_.replace(ScreenId::Home);
      after_nav();
    }
    return;
  }

  // Generic: Up/Down focus, Select/Back
  if (e == InputEvent::Up) {
    focus_.move(-1);
    mark_content_dirty();
  } else if (e == InputEvent::Down) {
    focus_.move(1);
    mark_content_dirty();
  } else if (e == InputEvent::Select) {
    nav_.replace(ScreenId::SettingsRoot);
    after_nav();
  }
}

}  // namespace pocket
