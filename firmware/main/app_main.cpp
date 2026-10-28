#include <sys/stat.h>
#include <string>

#include "pocket/app.hpp"
#include "pocket/input.hpp"
#include "pocket_board/axp.hpp"
#include "pocket_board/audio.hpp"
#include "pocket_board/buttons.hpp"
#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"
#include "pocket_board/sdcard.hpp"
#include "pocket_board/littlefs.hpp"
#include "esp_wifi_platform.hpp"
#include "esp_cloud_platform.hpp"
#include "esp_config_store.hpp"
#include "esp_ota_platform.hpp"

#ifdef POCKET_HOST
#error "app_main is for ESP-IDF only"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "driver/i2c_master.h"
#include <ctime>
#include "pocket/wall_clock.hpp"
#include "pocket_board/i2c_bus.hpp"

#include <sys/time.h>
#include <cstdlib>
#include <vector>

/** Survives deep sleep + soft reset; cleared on full power-loss. */
struct RtcWallAnchor {
  uint32_t magic;
  int64_t unix_sec;
  uint64_t mono_us;  // esp_timer_get_time at capture — only valid same boot
  uint64_t deep_us;  // esp_clk_rtc-style counter via gettimeofday boot offset
};
RTC_NOINIT_ATTR static RtcWallAnchor s_rtc_wall;
constexpr uint32_t kRtcWallMagic = 0x504B5257u;  // 'PKRW'

static const char* TAG = "pocket";

// Unique marker — must appear on Mac serial (cu.usbmodem) for this build.
static const char* kBuildId = "POCKET-LIVE-v96-dictation-retry";

namespace {

const char* reset_reason_str(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

/** Map IANA-ish ids used in onboarding to POSIX TZ for newlib. */
const char* posix_tz_for(const std::string& id) {
  if (id == "America/New_York") return "EST5EDT,M3.2.0,M11.1.0";
  if (id == "America/Chicago") return "CST6CDT,M3.2.0,M11.1.0";
  if (id == "America/Denver") return "MST7MDT,M3.2.0,M11.1.0";
  if (id == "America/Los_Angeles") return "PST8PDT,M3.2.0,M11.1.0";
  if (id == "America/Phoenix") return "MST7";
  if (id == "Europe/London") return "GMT0BST,M3.5.0/1,M10.5.0";
  if (id == "UTC" || id.empty()) return "UTC0";
  return "UTC0";
}

void apply_timezone(const std::string& tz_id) {
  setenv("TZ", posix_tz_for(tz_id), 1);
  tzset();
}

bool system_time_year_ok(time_t now) {
  struct tm t {};
  gmtime_r(&now, &t);
  return (t.tm_year + 1900) >= 2024;
}

void set_system_unix(int64_t unix_sec) {
  struct timeval tv {};
  tv.tv_sec = static_cast<time_t>(unix_sec);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
}

/** Optional external RTC (PCF8563 / BM8563 @ 0x51) — Waveshare 3.97 has none. */
bool hw_rtc_read_unix(int64_t* out_unix) {
  if (!out_unix) return false;
  i2c_master_bus_handle_t bus = pocket::board::i2c_bus();
  if (!bus) return false;
  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = 0x51;
  dev_cfg.scl_speed_hz = 100000;
  i2c_master_dev_handle_t dev = nullptr;
  if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK) return false;
  uint8_t reg = 0x02;
  uint8_t raw[7] = {};
  const esp_err_t err =
      i2c_master_transmit_receive(dev, &reg, 1, raw, sizeof(raw), 50);
  i2c_master_bus_rm_device(dev);
  if (err != ESP_OK) return false;
  auto bcd = [](uint8_t v) { return static_cast<int>(((v >> 4) & 0x0f) * 10 + (v & 0x0f)); };
  struct tm t {};
  t.tm_sec = bcd(raw[0] & 0x7f);
  t.tm_min = bcd(raw[1] & 0x7f);
  t.tm_hour = bcd(raw[2] & 0x3f);
  t.tm_mday = bcd(raw[3] & 0x3f);
  t.tm_wday = bcd(raw[4] & 0x07);
  t.tm_mon = bcd(raw[5] & 0x1f) - 1;
  t.tm_year = bcd(raw[6]) + 100;  // 2000+
  t.tm_isdst = -1;
  const time_t u = mktime(&t);
  if (u < 0 || !system_time_year_ok(u)) return false;
  *out_unix = static_cast<int64_t>(u);
  return true;
}

constexpr const char* kWallNvsNs = "pocket";
constexpr const char* kWallNvsKey = "wallclk";

bool nvs_load_wall(std::vector<uint8_t>& out) {
  nvs_handle_t h = 0;
  if (nvs_open(kWallNvsNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 0;
  esp_err_t err = nvs_get_blob(h, kWallNvsKey, nullptr, &len);
  if (err != ESP_OK || len == 0 || len > 64) {
    nvs_close(h);
    return false;
  }
  out.resize(len);
  err = nvs_get_blob(h, kWallNvsKey, out.data(), &len);
  nvs_close(h);
  if (err != ESP_OK) {
    out.clear();
    return false;
  }
  out.resize(len);
  return true;
}

bool nvs_save_wall(const std::vector<uint8_t>& blob) {
  if (blob.empty() || blob.size() > 64) return false;
  nvs_handle_t h = 0;
  if (nvs_open(kWallNvsNs, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(h, kWallNvsKey, blob.data(), blob.size());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

struct EspClock : pocket::PlatformClock {
  pocket::OfflineWallClock wall;
  uint32_t last_persist_ms_ = 0;
  esp_reset_reason_t reset_reason_ = ESP_RST_UNKNOWN;

  uint64_t mono_ms64() const {
    return static_cast<uint64_t>(esp_timer_get_time() / 1000LL);
  }
  /** Boot-local timebase stored in NVS snapshots (esp_timer µs). */
  uint64_t rtc_us64() const {
    return static_cast<uint64_t>(esp_timer_get_time());
  }

  void stamp_rtc_fast() {
    if (!wall.valid()) {
      s_rtc_wall.magic = 0;
      return;
    }
    s_rtc_wall.magic = kRtcWallMagic;
    s_rtc_wall.unix_sec = wall.now_unix(mono_ms64());
    s_rtc_wall.mono_us = static_cast<uint64_t>(esp_timer_get_time());
    s_rtc_wall.deep_us = s_rtc_wall.mono_us;  // placeholder; continuity via magic + reset reason
  }

  void apply_wall_to_system() {
    if (!wall.valid()) return;
    set_system_unix(wall.now_unix(mono_ms64()));
  }

  void persist_now() {
    wall.capture(mono_ms64(), rtc_us64());
    stamp_rtc_fast();
    std::vector<uint8_t> blob;
    if (!wall.pack(blob)) return;
    if (nvs_save_wall(blob)) {
      ESP_LOGI(TAG, "wall clock persisted quality=%d unix=%lld",
               static_cast<int>(wall.quality()),
               static_cast<long long>(wall.anchor_unix()));
    }
  }

  void boot_restore() {
    reset_reason_ = esp_reset_reason();
    // Prefer hardware RTC when present (PCF8563/BM8563); Waveshare 3.97 has none.
    int64_t hw = 0;
    if (hw_rtc_read_unix(&hw)) {
      wall.sync_trusted(hw, mono_ms64(), rtc_us64());
      apply_wall_to_system();
      persist_now();
      ESP_LOGI(TAG, "wall clock from hardware RTC unix=%lld", static_cast<long long>(hw));
      return;
    }
    // System time still valid after deep sleep (IDF keeps RTC calendar).
    const time_t sys = time(nullptr);
    if (system_time_year_ok(sys)) {
      wall.sync_trusted(static_cast<int64_t>(sys), mono_ms64(), rtc_us64());
      persist_now();
      ESP_LOGI(TAG, "wall clock from system RTC unix=%lld", static_cast<long long>(sys));
      return;
    }
    // RTC_NOINIT survived a soft reset (not deep sleep / power-on) — trusted resume.
    // Deep sleep relies on system calendar time above; power-on falls through to NVS.
    const bool soft_reset =
        reset_reason_ == ESP_RST_SW || reset_reason_ == ESP_RST_PANIC ||
        reset_reason_ == ESP_RST_INT_WDT || reset_reason_ == ESP_RST_TASK_WDT ||
        reset_reason_ == ESP_RST_WDT;
    if (soft_reset && s_rtc_wall.magic == kRtcWallMagic &&
        system_time_year_ok(static_cast<time_t>(s_rtc_wall.unix_sec))) {
      wall.sync_trusted(s_rtc_wall.unix_sec, mono_ms64(), rtc_us64());
      apply_wall_to_system();
      persist_now();
      ESP_LOGI(TAG, "wall clock from RTC_NOINIT unix=%lld",
               static_cast<long long>(s_rtc_wall.unix_sec));
      return;
    }
    if (reset_reason_ == ESP_RST_POWERON || reset_reason_ == ESP_RST_BROWNOUT) {
      s_rtc_wall.magic = 0;
    }
    std::vector<uint8_t> blob;
    if (!nvs_load_wall(blob)) {
      ESP_LOGI(TAG, "wall clock unsynced (no NVS snapshot)");
      return;
    }
    // Full power-loss: RTC timebases reset — Approximate from last NVS wall.
    if (!wall.restore(blob.data(), blob.size(), mono_ms64(), rtc_us64(),
                      /*rtc_continuous=*/false)) {
      ESP_LOGW(TAG, "wall clock NVS snapshot rejected");
      return;
    }
    apply_wall_to_system();
    stamp_rtc_fast();
    ESP_LOGI(TAG, "wall clock restored Approximate unix=%lld",
             static_cast<long long>(wall.now_unix(mono_ms64())));
  }

  /** Pull SNTP / system time into the offline wall clock when it looks valid. */
  void maybe_ingest_system_time() {
    const time_t sys = time(nullptr);
    if (!system_time_year_ok(sys)) return;
    const int64_t sys_i = static_cast<int64_t>(sys);
    if (!wall.trusted()) {
      wall.sync_trusted(sys_i, mono_ms64(), rtc_us64());
      persist_now();
      return;
    }
    const int64_t w = wall.now_unix(mono_ms64());
    const int64_t delta = sys_i > w ? sys_i - w : w - sys_i;
    if (delta > 2) {
      wall.sync_trusted(sys_i, mono_ms64(), rtc_us64());
      persist_now();
    }
  }

  void tick_persist(uint32_t now_ms) {
    maybe_ingest_system_time();
    if (!wall.valid()) return;
    if (last_persist_ms_ != 0 && (now_ms - last_persist_ms_) < 60'000u) return;
    last_persist_ms_ = now_ms;
    persist_now();
  }

  uint32_t now_ms() override {
    return static_cast<uint32_t>(esp_timer_get_time() / 1000LL);
  }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    maybe_ingest_system_time();
    const time_t now = time(nullptr);
    struct tm t {};
    localtime_r(&now, &t);
    hour = t.tm_hour;
    minute = t.tm_min;
    weekday = t.tm_wday;
    month = t.tm_mon;
    day = t.tm_mday;
  }
  bool time_valid() const override { return wall.valid(); }
  bool time_trusted() const override { return wall.trusted(); }
  bool time_needs_network_sync() const override { return wall.needs_network_sync(); }
  int battery_percent() override { return pocket::board::axp_battery_percent(); }
};

EspClock* g_esp_clock = nullptr;

void sntp_sync_notification(struct timeval* /*tv*/) {
  if (g_esp_clock) g_esp_clock->maybe_ingest_system_time();
}

void sntp_start_once() {
  static bool started = false;
  if (!started) {
    started = true;
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.cloudflare.com");
    esp_sntp_setservername(2, "time.google.com");
    esp_sntp_set_time_sync_notification_cb(sntp_sync_notification);
    esp_sntp_init();
    ESP_LOGI(TAG, "SNTP started");
  } else {
    // Re-sync after reconnect — first attempt may have failed before DNS.
    esp_sntp_restart();
    ESP_LOGI(TAG, "SNTP restarted");
  }
}

struct EspDisplay : pocket::PlatformDisplay {
  pocket::board::EpdDisplay& epd;
  explicit EspDisplay(pocket::board::EpdDisplay& e) : epd(e) {}
  void present(const pocket::Canvas& c, pocket::RefreshMode mode) override {
    epd.present(c, mode);
  }
  void present_region(const pocket::Canvas& c, int x, int y, int w, int h) override {
    epd.present_region(c, x, y, w, h);
  }
  bool busy() override { return epd.busy(); }
  void set_brightness(int percent) override { epd.set_brightness(percent); }
};

struct EspStorage : pocket::PlatformStorage {
  bool probe() override { return pocket::board::sd_probe(); }
  bool present() override { return pocket::board::sd_present(); }
  pocket::SdContentKind classify() override {
    switch (pocket::board::sd_classify()) {
      case pocket::board::SdContentKind::Absent:
        return pocket::SdContentKind::Absent;
      case pocket::board::SdContentKind::Empty:
        return pocket::SdContentKind::Empty;
      case pocket::board::SdContentKind::Media:
        return pocket::SdContentKind::Media;
      case pocket::board::SdContentKind::FirmwareRisk:
        return pocket::SdContentKind::FirmwareRisk;
      default:
        return pocket::SdContentKind::Unknown;
    }
  }
  bool erase_card() override { return pocket::board::sd_erase(); }
  void unmount() override { pocket::board::sd_unmount(); }

  std::string music_root() override {
    if (pocket::board::sd_present()) return "/sdcard/pocket/music";
    if (pocket::board::littlefs_mounted() || pocket::board::littlefs_mount()) return "/littlefs/music";
    return {};
  }
  bool music_ensure_root() override {
    std::string root = music_root();
    if (root.empty()) return false;
    // Create parent dirs
    if (root.rfind("/sdcard", 0) == 0) {
      mkdir("/sdcard/pocket", 0755);
      mkdir("/sdcard/pocket/music", 0755);
    } else {
      mkdir("/littlefs/music", 0755);
    }
    const uint64_t free = free_bytes(root.rfind("/sdcard", 0) == 0 ? "/sdcard" : "/littlefs");
    return free > 128 * 1024;
  }
  uint64_t free_bytes(const std::string& root) override {
    return pocket::board::fs_free_bytes(root.c_str());
  }
  bool music_on_sd() override {
    const std::string root = music_root();
    return root.rfind("/sdcard", 0) == 0;
  }

  std::string book_root() override {
    if (pocket::board::sd_present()) return "/sdcard/pocket/books";
    if (pocket::board::littlefs_mounted() || pocket::board::littlefs_mount()) return "/littlefs/books";
    return {};
  }
  bool book_ensure_root() override {
    std::string root = book_root();
    if (root.empty()) return false;
    if (root.rfind("/sdcard", 0) == 0) {
      mkdir("/sdcard/pocket", 0755);
      mkdir("/sdcard/pocket/books", 0755);
    } else {
      mkdir("/littlefs/books", 0755);
    }
    const uint64_t free = free_bytes(root.rfind("/sdcard", 0) == 0 ? "/sdcard" : "/littlefs");
    return free > 128 * 1024;
  }

  std::string data_root() override {
    // Prefer internal LittleFS; fall back to SD so Notes/Lists/Passes/weather
    // cache stay usable offline when the data partition isn't mounted.
    if (pocket::board::littlefs_mounted() || pocket::board::littlefs_mount()) return "/littlefs";
    if (pocket::board::sd_present()) return "/sdcard/pocket";
    return {};
  }
  bool data_ensure_root() override {
    const std::string root = data_root();
    if (root.empty()) return false;
    if (root.rfind("/sdcard", 0) == 0) {
      mkdir("/sdcard/pocket", 0755);
    }
    mkdir((root + "/notes").c_str(), 0755);
    mkdir((root + "/lists").c_str(), 0755);
    mkdir((root + "/passes").c_str(), 0755);
    mkdir((root + "/weather").c_str(), 0755);
    return true;
  }
};

struct EspAudio : pocket::PlatformAudio {
  void play(pocket::SoundId id) override {
    pocket::board::SoundId bid = pocket::board::SoundId::Click;
    switch (id) {
      case pocket::SoundId::Welcome:
        bid = pocket::board::SoundId::Welcome;
        break;
      case pocket::SoundId::Success:
        bid = pocket::board::SoundId::Success;
        break;
      case pocket::SoundId::Attention:
        bid = pocket::board::SoundId::Attention;
        break;
      default:
        bid = pocket::board::SoundId::Click;
        break;
    }
    pocket::board::audio_play(bid);
  }
  bool play_file(const std::string& path) override {
    return pocket::board::audio_play_wav_file(path.c_str());
  }
  void stop() override { pocket::board::audio_stop(); }
  void set_volume(int percent) override {
    pocket::board::audio_set_volume(static_cast<uint8_t>(percent < 0 ? 0 : (percent > 100 ? 100 : percent)));
  }
  bool mic_supported() override { return pocket::board::audio_mic_supported(); }
  void start_capture() override { pocket::board::audio_capture_start(); }
  void poll_capture() override { pocket::board::audio_capture_poll(); }
  pocket::MicCaptureResult stop_capture() override {
    const pocket::board::MicCaptureResult r = pocket::board::audio_capture_stop();
    pocket::MicCaptureResult out;
    out.ok = r.ok;
    out.level_percent = r.level_percent;
    out.pcm = r.pcm;
    return out;
  }
};

struct EspIdentity : pocket::PlatformIdentity {
  std::string device_uuid() override {
    uint8_t mac[6] = {};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x-%02x%02x-4000-80%02x-%02x%02x%02x%02x%02x%02x",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[0] ^ 0x80, mac[1], mac[2], mac[3],
                  mac[4], mac[5], static_cast<uint8_t>(mac[0] + mac[5]));
    return buf;
  }
};

struct BootCtx {
  pocket::board::ButtonPoller* buttons = nullptr;
  pocket::board::EpdDisplay* epd = nullptr;
  pocket::App* app = nullptr;
  pocket::InputMapper* mapper = nullptr;
  EspClock* clock = nullptr;
  bool ui_ready = false;
};

static BootCtx g_boot;

static void epd_boot_task(void* /*arg*/) {
  esp_rom_printf("epd_boot_task: axp\n");
  ESP_LOGI(TAG, "epd_boot_task: enabling AXP");
  pocket::board::axp_enable_epd_rails();

  // Codec after rails — AVDD/PA need a stable supply for audible playback.
  esp_rom_printf("epd_boot_task: audio\n");
  (void)pocket::board::audio_init();

  esp_rom_printf("epd_boot_task: epd.init\n");
  ESP_LOGI(TAG, "epd_boot_task: e-paper init / factory wipe");
  if (g_boot.epd && !g_boot.epd->init()) {
    ESP_LOGE(TAG, "e-paper init failed — UI stays headless");
    vTaskDelete(nullptr);
    return;
  }

  esp_rom_printf("epd_boot_task: app.boot\n");
  ESP_LOGI(TAG, "epd_boot_task: painting Welcome");
  if (g_boot.app) {
    apply_timezone(g_boot.app->config().tz_id);
    g_boot.app->boot();
    apply_timezone(g_boot.app->config().tz_id);
    ESP_LOGI(TAG, "UI boot complete, screen=%d", static_cast<int>(g_boot.app->screen()));
  }
  g_boot.ui_ready = true;
  esp_rom_printf("*** %s READY ***\n", kBuildId);
  vTaskDelete(nullptr);
}

}  // namespace

// Called from EspWifi::connect after STA association succeeds.
extern "C" void pocket_on_wifi_connected(void) {
  if (g_boot.app) apply_timezone(g_boot.app->config().tz_id);
  sntp_start_once();
  if (g_boot.app) g_boot.app->notify_wifi_connected();
}

extern "C" void app_main(void) {
  esp_rom_printf("\n*** %s ***\n", kBuildId);

  const esp_reset_reason_t rr = esp_reset_reason();
  ESP_LOGI(TAG, "%s reset=%s (%d)", kBuildId, reset_reason_str(rr), static_cast<int>(rr));

  for (int i = 0; i < 3; ++i) {
    esp_rom_printf("heartbeat %d/3\n", i + 1);
    ESP_LOGI(TAG, "heartbeat %d/3", i + 1);
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  // Prefer SD for media; mount internal LittleFS as music fallback (~2 MiB).
  (void)pocket::board::sd_probe();
  if (!pocket::board::littlefs_mount()) {
    ESP_LOGW(TAG, "LittleFS mount failed — music needs SD if internal unavailable");
  }

  ESP_LOGI(TAG, "Pocket boot — canvas %dx%d Up=%d Fn=%d Down=%d", pocket::board::kLogicalW,
           pocket::board::kLogicalH, pocket::board::kPinButtonUp, pocket::board::kPinButtonFunction,
           pocket::board::kPinButtonDown);

  static pocket::board::ButtonPoller buttons;
  buttons.init();

  static pocket::board::EpdDisplay epd;
  // Durable config: NVS always; SD mirror at /sdcard/pocket/config.bin when usable.
  static EspPersistentConfigStore store;
  static EspClock clock;
  g_esp_clock = &clock;
  clock.boot_restore();
  static EspWifi wifi;
  static EspCloud cloud;
  static EspOta ota;
  static EspDisplay display(epd);
  static EspStorage storage;
  static EspAudio audio;
  static EspIdentity identity;
  static pocket::InputMapper mapper;
  static pocket::App app(store, clock, wifi, cloud, display, &storage, &audio, &identity, &ota);
  app.set_build_id(kBuildId);

  g_boot.buttons = &buttons;
  g_boot.epd = &epd;
  g_boot.app = &app;
  g_boot.mapper = &mapper;
  g_boot.clock = &clock;

  xTaskCreate(epd_boot_task, "epd_boot", 8192, nullptr, 5, nullptr);

  ESP_LOGI(TAG, "input loop running (EPD wipe in background)");
  uint32_t last_hb = 0;
  std::string last_tz;
  while (true) {
    const uint32_t now = clock.now_ms();
    if (now - last_hb > 5000) {
      esp_rom_printf("loop alive ui_ready=%d batt=%d time_ok=%d\n", g_boot.ui_ready ? 1 : 0,
                     clock.battery_percent(), clock.time_valid() ? 1 : 0);
      last_hb = now;
    }
    if (g_boot.ui_ready) {
      const std::string& tz = app.config().tz_id;
      if (tz != last_tz) {
        apply_timezone(tz);
        last_tz = tz;
      }
    }
    buttons.poll(mapper, now);
    for (;;) {
      const pocket::InputEvent e = mapper.poll();
      if (e == pocket::InputEvent::None) break;
      if (!g_boot.ui_ready) continue;
      // Drain already-queued Up/Down (bounce / extra detents) into logical state
      // immediately. Defer present until after the burst so a busy panel never
      // drops the second PIN tick waiting on a 0xFF partial.
      if (e == pocket::InputEvent::Up || e == pocket::InputEvent::Down) {
        int delta = (e == pocket::InputEvent::Down) ? 1 : -1;
        delta += mapper.drain_up_down_net();
        app.apply_coalesced_rotary(delta);
        continue;
      }
      // Flush any coalesced rotary before Select/Back so the digit on screen
      // matches the committed value (no-op while the panel is still busy).
      app.flush_dirty();
      app.handle(e);
    }
    if (g_boot.ui_ready) {
      app.flush_dirty();
      app.tick(now);
    }
    clock.tick_persist(now);
    // 5 ms — snappier input without busy-spinning the core (was 10 ms after v67).
    vTaskDelay(pdMS_TO_TICKS(5));
  }
}
