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

#ifdef POCKET_HOST
#error "app_main is for ESP-IDF only"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "esp_sntp.h"

static const char* TAG = "pocket";

// Unique marker — must appear on Mac serial (cu.usbmodem) for this build.
static const char* kBuildId = "POCKET-LIVE-v31b-firmware-refresh";

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

struct EspClock : pocket::PlatformClock {
  uint32_t now_ms() override {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    const time_t now = time(nullptr);
    struct tm t {};
    localtime_r(&now, &t);
    hour = t.tm_hour;
    minute = t.tm_min;
    weekday = t.tm_wday;
    month = t.tm_mon;
    day = t.tm_mday;
  }
  bool time_valid() const override {
    const time_t now = time(nullptr);
    struct tm t {};
    gmtime_r(&now, &t);
    return (t.tm_year + 1900) >= 2024;
  }
  int battery_percent() override { return pocket::board::axp_battery_percent(); }
};

void sntp_start_once() {
  static bool started = false;
  if (started) return;
  started = true;
  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();
  ESP_LOGI(TAG, "SNTP started");
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
extern "C" void pocket_on_wifi_connected(void) { sntp_start_once(); }

extern "C" void app_main(void) {
  esp_rom_printf("\n*** %s ***\n", kBuildId);

  const esp_reset_reason_t rr = esp_reset_reason();
  ESP_LOGI(TAG, "%s reset=%s (%d)", kBuildId, reset_reason_str(rr), static_cast<int>(rr));

  for (int i = 0; i < 3; ++i) {
    esp_rom_printf("heartbeat %d/3\n", i + 1);
    ESP_LOGI(TAG, "heartbeat %d/3", i + 1);
    vTaskDelay(pdMS_TO_TICKS(200));
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
  static EspWifi wifi;
  static EspCloud cloud;
  static EspDisplay display(epd);
  static EspStorage storage;
  static EspAudio audio;
  static EspIdentity identity;
  static pocket::InputMapper mapper;
  static pocket::App app(store, clock, wifi, cloud, display, &storage, &audio, &identity);

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
      if (g_boot.ui_ready) app.handle(e);
    }
    if (g_boot.ui_ready) app.tick(now);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
