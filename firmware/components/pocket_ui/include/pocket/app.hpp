#pragma once
#include "pocket/canvas.hpp"
#include "pocket/config.hpp"
#include "pocket/input.hpp"
#include "pocket/nav.hpp"
#include "pocket/refresh.hpp"
#include <memory>
#include <string>
#include <vector>

namespace pocket {

struct PlatformClock {
  virtual ~PlatformClock() = default;
  virtual uint32_t now_ms() = 0;
  virtual void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) = 0;
  /** True once wall clock is valid (e.g. after SNTP). */
  virtual bool time_valid() const { return true; }
  /** Battery 0–100 from PMIC; USB-only boards may report 100. */
  virtual int battery_percent() { return 100; }
};

struct PlatformWifi {
  virtual ~PlatformWifi() = default;
  virtual std::vector<std::string> scan() = 0;
  virtual bool connect(const std::string& ssid, const std::string& password) = 0;
  virtual bool connected() const = 0;
  /** SoftAP + HTTP portal so the phone can send Wi‑Fi credentials (no on-device typing). */
  virtual bool start_provision(const std::string& preferred_ssid, std::string* ap_ssid_out,
                               std::string* ap_pass_out) = 0;
  virtual void stop_provision() = 0;
  virtual bool take_provision_credentials(std::string* ssid, std::string* password) = 0;
  virtual std::string provision_ap_ssid() const = 0;
  virtual std::string provision_ap_password() const = 0;
  /** SoftAP portal currently advertising (phone can join). */
  virtual bool provisioning() const { return false; }
};

struct PlatformCloud {
  virtual ~PlatformCloud() = default;
  /** Mint pairing session; returns plaintext code for QR display. */
  virtual std::string create_pair_session(const std::string& device_id) = 0;
  virtual std::string pair_status(const std::string& code) = 0;  // pending|claimed|expired
  virtual void refresh_entitlement(DeviceConfig& cfg) = 0;
  virtual std::string stt_transcribe(const std::vector<uint8_t>& pcm) = 0;
};

struct PlatformDisplay {
  virtual ~PlatformDisplay() = default;
  virtual void present(const Canvas& canvas, RefreshMode mode) = 0;
  /** Partial refresh of a logical rectangle (clock tick). Falls back to full-canvas partial. */
  virtual void present_region(const Canvas& canvas, int /*x*/, int /*y*/, int /*w*/, int /*h*/) {
    present(canvas, RefreshMode::Partial);
  }
};

enum class SdContentKind : uint8_t {
  Absent = 0,
  Empty,
  Media,
  FirmwareRisk,
  Unknown,
};

struct PlatformStorage {
  virtual ~PlatformStorage() = default;
  virtual bool probe() { return false; }
  virtual bool present() { return false; }
  virtual SdContentKind classify() { return SdContentKind::Absent; }
  virtual bool erase_card() { return false; }
  virtual void unmount() {}
};

enum class SoundId : uint8_t { Click = 0, Welcome, Success, Attention };

struct PlatformAudio {
  virtual ~PlatformAudio() = default;
  virtual void play(SoundId /*id*/) {}
};

/** Optional: stable device UUID from MAC (ESP). Empty → App generates placeholder. */
struct PlatformIdentity {
  virtual ~PlatformIdentity() = default;
  virtual std::string device_uuid() { return {}; }
};

struct Note {
  std::string id;
  std::string title;
  std::string body;
  int64_t created_at = 0;
  int64_t updated_at = 0;
};

struct ListItem {
  std::string id;
  std::string text;
  bool checked = false;
  int64_t updated_at = 0;
};

struct TodoList {
  std::string id;
  std::string title;
  std::vector<ListItem> items;
  int64_t updated_at = 0;
};

struct Alarm {
  std::string id;
  int minutes_of_day = 0;
  bool enabled = true;
  std::string label;
};

struct Pass {
  std::string id;
  std::string title;
  std::string type;
  std::string payload;
};

struct WeatherDay {
  std::string date;
  int hi = 0;
  int lo = 0;
  std::string condition;
};

struct WeatherCache {
  std::string city;
  uint8_t units = 0;
  int64_t fetched_at = 0;
  std::vector<WeatherDay> days;
  int today_temp = 0;
  std::string today_condition;
};

struct AppData {
  std::vector<Note> notes;
  std::vector<TodoList> lists;
  std::vector<Alarm> alarms;
  std::vector<Pass> passes;
  WeatherCache weather;
};

/** Focusable row helper for list UIs. */
struct FocusModel {
  int index = 0;
  int count = 0;
  void move(int delta) {
    if (count <= 0) {
      index = 0;
      return;
    }
    index = (index + delta) % count;
    if (index < 0) index += count;
  }
};

class App {
 public:
  App(ConfigStore& store, PlatformClock& clock, PlatformWifi& wifi, PlatformCloud& cloud,
      PlatformDisplay& display, PlatformStorage* storage = nullptr, PlatformAudio* audio = nullptr,
      PlatformIdentity* identity = nullptr);

  void boot();
  void tick(uint32_t now_ms);
  void handle(InputEvent e);

  ScreenId screen() const { return nav_.current(); }
  const DeviceConfig& config() const { return cfg_; }
  const Canvas& canvas() const { return canvas_; }
  bool needs_redraw() const { return dirty_; }

  /** Force render for host/tests. */
  void redraw(bool full);

 private:
  enum class DirtyKind : uint8_t { FullCanvas, ContentBand, StatusBar, Region };

  void render();
  void draw_status_bar();
  void go_home();
  void go_lock();
  void after_nav(bool full_refresh);
  /** Navigate then refresh using Spec §6 (full only for major enters / QR / PIN). */
  void after_nav();
  void mark_content_dirty();
  void mark_status_dirty();
  /** Tight e-ink region update (PIN digits, focus rows, etc.). */
  void mark_region_dirty(int x, int y, int w, int h);
  void mark_pin_dirty();
  void present_canvas(bool full);
  void play_sound(SoundId id);
  bool mint_pair_session();
  void begin_softap_link();
  /** Draw PIN slots; mask_completed hides entered digits as dots (unlock). */
  void draw_pin_entry(bool mask_completed, int band_top);
  void pin_band_geometry(int& x, int& y, int& w, int& h) const;

  // Screen handlers
  void render_lock();
  void handle_lock(InputEvent e);
  void render_pin();
  void handle_pin(InputEvent e);
  void render_onboarding();
  void handle_onboarding(InputEvent e);
  void render_home();
  void handle_home(InputEvent e);
  void draw_home_clock();
  void draw_lock_motif();
  void maybe_tick_home_clock();
  void present_home_clock_partial();
  void render_notes();
  void handle_notes(InputEvent e);
  void render_ledger();
  void handle_ledger(InputEvent e);
  void render_clock();
  void handle_clock(InputEvent e);
  void render_pass();
  void handle_pass(InputEvent e);
  void render_weather();
  void handle_weather(InputEvent e);
  void render_settings();
  void handle_settings(InputEvent e);

  ConfigStore& store_;
  PlatformClock& clock_;
  PlatformWifi& wifi_;
  PlatformCloud& cloud_;
  PlatformDisplay& display_;
  PlatformStorage* storage_ = nullptr;
  PlatformAudio* audio_ = nullptr;
  PlatformIdentity* identity_ = nullptr;

  DeviceConfig cfg_{};
  AppData data_{};
  NavStack nav_{};
  Canvas canvas_{};
  RefreshPolicy refresh_{};
  InputMapper input_{};  // unused when events injected externally
  bool dirty_ = true;
  DirtyKind dirty_kind_ = DirtyKind::FullCanvas;
  int dirty_rx_ = 0;
  int dirty_ry_ = 0;
  int dirty_rw_ = 0;
  int dirty_rh_ = 0;

  // UI transient state
  FocusModel focus_{};
  std::string pin_entry_;
  std::string pin_pending_;
  char pin_digit_working_ = '0';
  int pin_fail_count_ = 0;
  uint32_t pin_lockout_until_ms_ = 0;
  uint32_t error_until_ms_ = 0;
  std::string error_msg_;
  std::string wifi_password_;
  std::vector<std::string> wifi_networks_;
  std::string wifi_ap_ssid_;
  std::string wifi_ap_pass_;
  uint32_t last_wifi_prov_poll_ms_ = 0;
  std::string pair_code_;
  uint32_t pair_expires_ms_ = 0;
  uint32_t last_pair_poll_ms_ = 0;
  std::string pair_status_ = "pending";
  int charset_index_ = 0;
  int caret_ = 0;
  std::string picker_buf_;
  int notes_tab_ = 0;  // 0 Notes 1 Lists
  int clock_tab_ = 0;
  int note_index_ = 0;
  uint32_t last_input_ms_ = 0;
  uint32_t now_ms_ = 0;
  bool ptt_active_ = false;
  std::string mic_result_;
  int onboarding_tz_index_ = 0;
  int last_home_clock_minute_ = -1;
  SdContentKind sd_kind_ = SdContentKind::Absent;
  bool sd_waiting_eject_ = false;
  uint32_t last_sd_poll_ms_ = 0;
  bool welcome_sound_played_ = false;
};

}  // namespace pocket