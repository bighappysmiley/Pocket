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

/** Number of calm, intentionally-themed motifs the lock/sleep face rotates through:
 * horizon line, tide rings, folded paper plane, quiet constellation, city grid at
 * dusk, and aperture. Each is a designed composition, not generic abstract lines. */
inline constexpr int kLockMotifCount = 6;

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
  /** List music library metadata JSON (id/title/filename/size). Empty on failure. */
  virtual std::string music_list_json(const std::string& /*device_id*/, bool /*sd_present*/ = false) {
    return "[]";
  }
  /** Download track audio bytes by id. Returns empty on failure. */
  virtual std::vector<uint8_t> music_download(const std::string& /*device_id*/,
                                              const std::string& /*track_id*/) {
    return {};
  }
  /** List Reading library metadata JSON (id/title/author/format/filename/size). Empty on failure. */
  virtual std::string books_list_json(const std::string& /*device_id*/) { return "[]"; }
  /** Download the normalized plain-text body of a book by id. Empty on failure. */
  virtual std::vector<uint8_t> book_download(const std::string& /*device_id*/,
                                             const std::string& /*book_id*/) {
    return {};
  }
  /** Latest firmware metadata JSON: build_id, version, url, size. Empty on failure. */
  virtual std::string firmware_latest_json() { return {}; }
  /**
   * Heartbeat + entitlement + optional pending Wi‑Fi / parental JSON.
   * Returns raw JSON body; empty on failure.
   */
  virtual std::string device_attest_json(const std::string& /*device_id*/) { return {}; }
  /** Best-effort mirror of a rotary-set volume/brightness change up to Companion. Fire-and-forget. */
  virtual bool push_device_settings(const std::string& /*device_id*/, int /*volume_percent*/,
                                    int /*brightness_percent*/) {
    return false;
  }
};

struct FirmwareUpdateInfo {
  std::string build_id;
  std::string version;
  std::string url;
  int size_bytes = 0;
};

struct PlatformOta {
  virtual ~PlatformOta() = default;
  /** Download `url` into the inactive OTA slot and set boot partition. Blocking. */
  virtual bool apply_https_ota(const std::string& url, std::string* status_out) {
    if (status_out) *status_out = "OTA not available.";
    return false;
  }
};

struct PlatformDisplay {
  virtual ~PlatformDisplay() = default;
  virtual void present(const Canvas& canvas, RefreshMode mode) = 0;
  /** Partial refresh of a logical rectangle (clock tick). Falls back to full-canvas partial. */
  virtual void present_region(const Canvas& canvas, int /*x*/, int /*y*/, int /*w*/, int /*h*/) {
    present(canvas, RefreshMode::Partial);
  }
  /** 0–100: how aggressively midtone gray rounds to black vs white on a monochrome panel.
   * Higher reads lighter/brighter, lower reads darker/higher-contrast. No-op on displays that
   * don't expose this (e.g. host sim). */
  virtual void set_brightness(int /*percent*/) {}
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
  /** Prefer SD when mounted + space; else LittleFS internal. Empty if neither usable. */
  virtual std::string music_root() { return {}; }
  virtual bool music_ensure_root() { return false; }
  virtual uint64_t free_bytes(const std::string& /*root*/) { return 0; }
  /** True when music_root is on the microSD mount (larger tracks OK). */
  virtual bool music_on_sd() { return false; }
  /** Prefer SD when mounted; else LittleFS internal. Empty if neither usable. */
  virtual std::string book_root() { return {}; }
  virtual bool book_ensure_root() { return false; }
};

enum class SoundId : uint8_t { Click = 0, Welcome, Success, Attention };

/** Result of a PTT-bounded mic capture window (Settings → Sound → Mic test, onboarding Voice step). */
struct MicCaptureResult {
  /** True when the board has a working mic path and captured at least one chunk. */
  bool ok = false;
  /** 0–100 loudness estimate (RMS) across the whole hold — real signal, not simulated. */
  int level_percent = 0;
  /** Raw 16-bit PCM mono samples captured (16 kHz), for optional cloud dictation. */
  std::vector<uint8_t> pcm;
};

struct PlatformAudio {
  virtual ~PlatformAudio() = default;
  virtual void play(SoundId /*id*/) {}
  /** Play a PCM/WAV file from local path (best-effort; may block). */
  virtual bool play_file(const std::string& /*path*/) { return false; }
  virtual void stop() {}
  /** DAC output level, 0–100. Applied immediately when the codec is ready. */
  virtual void set_volume(int /*percent*/) {}
  /** True when this board exposes a real mic capture path (onboard PDM/I2S mic + codec ADC). */
  virtual bool mic_supported() { return false; }
  /** Begin a capture window (call on PTT down). Safe no-op if unsupported. */
  virtual void start_capture() {}
  /** Non-blocking: pull whatever PCM has accumulated since start/last poll (call from tick). */
  virtual void poll_capture() {}
  /** End the capture window (call on PTT up) and return the accumulated result. */
  virtual MicCaptureResult stop_capture() { return {}; }
};

struct MusicTrack {
  std::string id;
  std::string title;
  std::string filename;
  std::string local_path;
  int size_bytes = 0;
};

/** eBook metadata + device-local copy. Device always reads plain text; `format` records the
 * original upload so Companion/UI can show it (EPUB text is normalized server-side). */
struct Book {
  std::string id;
  std::string title;
  std::string author;
  std::string format;  // "epub" | "txt"
  std::string filename;
  std::string local_path;
  int size_bytes = 0;
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
      PlatformIdentity* identity = nullptr, PlatformOta* ota = nullptr);

  void boot();
  void set_build_id(std::string_view id);
  void tick(uint32_t now_ms);
  void handle(InputEvent e);

  ScreenId screen() const { return nav_.current(); }
  const DeviceConfig& config() const { return cfg_; }
  /** Which of kLockMotifCount calm designs the lock/sleep face is currently showing. */
  int lock_motif_index() const { return lock_motif_index_; }
  const Canvas& canvas() const { return canvas_; }
  bool needs_redraw() const { return dirty_; }

  /** STA just associated — refresh clock sync + Cloud heartbeat promptly. */
  void notify_wifi_connected();

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
  /** Resume mid-setup from saved DeviceConfig fields (avoid Welcome loop). */
  ScreenId resume_onboarding_screen() const;
  void ensure_softap_credentials_shown();
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
  /** Small geometric mark to the left of the "Pocket" wordmark on the lock face. */
  void draw_pocket_mark(int right_x, int cy);
  void maybe_tick_home_clock();
  void present_home_clock_partial();
  /** Status bar time / battery / wifi — region partial, not full screen. */
  void maybe_tick_status_chrome();
  /** Clock app huge time — region partial on minute change. */
  void maybe_tick_clock_face();
  bool screen_has_status_bar() const;
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
  void render_music();
  void handle_music(InputEvent e);
  void music_sync_from_cloud();
  void render_reading();
  void handle_reading(InputEvent e);
  void reading_sync_from_cloud();
  /** Load a book's text, compute e-ink page boundaries, resume last page if it matches. */
  void reading_open_book(int index);
  /** Recompute `reading_pages_` word-wrap boundaries for `reading_text_` at content width. */
  void reading_paginate();
  void render_settings();
  void handle_settings(InputEvent e);
  void render_sd_gate();
  void handle_sd_gate(InputEvent e);
  void enter_sd_gate_from_hotplug();
  void leave_sd_gate();
  void maybe_poll_sd_hotplug();
  void refresh_sd_kind();
  void begin_firmware_update();
  bool parse_firmware_latest(const std::string& json, FirmwareUpdateInfo* out);
  /** Persist SSID+pass and attempt STA connect (blocking). */
  bool connect_and_remember(const std::string& ssid, const std::string& password);
  /** After boot / when idle: try preferred then other known networks. */
  void maybe_wifi_auto_reconnect();
  /** Start SoftAP to add another known network (keeps existing list). */
  void begin_add_wifi_network();
  /** Poll Cloud when online: last_seen, entitlement, pending Wi‑Fi, parental. */
  void maybe_cloud_attest();
  bool parental_requires_pin(HomeApp app) const;
  void launch_home_app(HomeApp app);

  ConfigStore& store_;
  PlatformClock& clock_;
  PlatformWifi& wifi_;
  PlatformCloud& cloud_;
  PlatformDisplay& display_;
  PlatformStorage* storage_ = nullptr;
  PlatformAudio* audio_ = nullptr;
  PlatformIdentity* identity_ = nullptr;
  PlatformOta* ota_ = nullptr;

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
  /** SoftAP screen is connecting to home Wi‑Fi (no Connecting screen remount). */
  bool wifi_sta_connecting_ = false;
  uint32_t last_wifi_prov_poll_ms_ = 0;
  uint32_t last_wifi_reconnect_ms_ = 0;
  bool wifi_boot_reconnect_done_ = false;
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
  int music_index_ = 0;
  std::vector<MusicTrack> music_tracks_;
  bool music_playing_ = false;
  std::string music_status_;
  int book_index_ = 0;
  std::vector<Book> books_;
  std::string reading_status_;
  std::string reading_text_;
  std::vector<size_t> reading_pages_;
  int reading_page_ = 0;
  uint32_t last_input_ms_ = 0;
  uint32_t now_ms_ = 0;
  bool ptt_active_ = false;
  std::string mic_result_;
  /** Last mic-test loudness 0–100, -1 = not yet tested this session. */
  int mic_last_level_ = -1;
  bool mic_last_ok_ = false;
  /** Which screen armed the current PTT hold, so tick() knows whether to poll capture. */
  bool mic_capturing_ = false;
  int onboarding_tz_index_ = 0;
  int last_home_clock_minute_ = -1;
  int last_status_minute_ = -1;
  int last_status_battery_ = -1;
  bool last_status_wifi_ = false;
  bool last_status_time_ok_ = false;
  /** Which calm e-ink motif to draw on the lock/sleep face; advances on each lock. */
  int lock_motif_index_ = 0;
  SdContentKind sd_kind_ = SdContentKind::Absent;
  bool sd_waiting_eject_ = false;
  uint32_t last_sd_poll_ms_ = 0;
  /** Edge-detect hot-insert after onboarding (true while card still seated after gate). */
  bool sd_was_present_ = false;
  bool sd_gate_active_ = false;
  ScreenId sd_return_screen_ = ScreenId::Home;
  std::string ota_status_;
  std::string pending_build_id_;
  bool welcome_sound_played_ = false;
  uint32_t last_attest_ms_ = 0;
  /** After unlock PIN for a parental-gated app, allow launches until lock. */
  bool parental_session_unlocked_ = false;
  bool parental_pin_for_app_ = false;
  HomeApp parental_pending_app_ = HomeApp::Notes;
  /** OnboardingDone / Tips pages (0=ready, 1=rotary, 2=side, 3=power). */
  int tips_page_ = 0;
  /** Settings → Wi‑Fi: 0 = known list, 1 = Add (hotspot / another), 2 = Remove picker. */
  uint8_t wifi_ui_page_ = 0;
};

}  // namespace pocket