#include <stdio.h>
#include "pocket/app.hpp"
#include "pocket/input.hpp"
#include "pocket_board/axp.hpp"
#include "pocket_board/buttons.hpp"
#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"
#include "esp_wifi_platform.hpp"
#include "esp_cloud_platform.hpp"

#ifdef POCKET_HOST
#error "app_main is for ESP-IDF only"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"

static const char* TAG = "pocket";

// Unique marker — must appear on Mac serial (cu.usbmodem) for this build.
static const char* kBuildId = "POCKET-LIVE-v15-softap";

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

struct EspClock : pocket::PlatformClock {
  uint32_t now_ms() override {
    return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 12;
    minute = 0;
    weekday = 0;
    month = 0;
    day = 1;
  }
};

struct EspDisplay : pocket::PlatformDisplay {
  pocket::board::EpdDisplay& epd;
  explicit EspDisplay(pocket::board::EpdDisplay& e) : epd(e) {}
  void present(const pocket::Canvas& c, pocket::RefreshMode mode) override {
    epd.present(c, mode);
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
    g_boot.app->boot();
    ESP_LOGI(TAG, "UI boot complete, screen=%d", static_cast<int>(g_boot.app->screen()));
  }
  g_boot.ui_ready = true;
  esp_rom_printf("*** %s READY ***\n", kBuildId);
  vTaskDelete(nullptr);
}

}  // namespace

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

  ESP_LOGI(TAG, "Pocket boot — canvas %dx%d Up=%d Fn=%d Down=%d", pocket::board::kLogicalW,
           pocket::board::kLogicalH, pocket::board::kPinButtonUp, pocket::board::kPinButtonFunction,
           pocket::board::kPinButtonDown);

  static pocket::board::ButtonPoller buttons;
  buttons.init();

  static pocket::board::EpdDisplay epd;
  static pocket::MemoryConfigStore store;
  static EspClock clock;
  static EspWifi wifi;
  static EspCloud cloud;
  static EspDisplay display(epd);
  static pocket::InputMapper mapper;
  static pocket::App app(store, clock, wifi, cloud, display);

  g_boot.buttons = &buttons;
  g_boot.epd = &epd;
  g_boot.app = &app;
  g_boot.mapper = &mapper;
  g_boot.clock = &clock;

  xTaskCreate(epd_boot_task, "epd_boot", 8192, nullptr, 5, nullptr);

  ESP_LOGI(TAG, "input loop running (EPD wipe in background)");
  uint32_t last_hb = 0;
  while (true) {
    const uint32_t now = clock.now_ms();
    if (now - last_hb > 5000) {
      esp_rom_printf("loop alive ui_ready=%d\n", g_boot.ui_ready ? 1 : 0);
      last_hb = now;
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
