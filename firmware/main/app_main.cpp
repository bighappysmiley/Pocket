#include <stdio.h>
#include "pocket/app.hpp"
#include "pocket/input.hpp"
#include "pocket_board/axp.hpp"
#include "pocket_board/buttons.hpp"
#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"

#ifdef POCKET_HOST
#error "app_main is for ESP-IDF only"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"

static const char* TAG = "pocket";

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

struct EspWifi : pocket::PlatformWifi {
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return false; }
  bool connected() const override { return false; }
};

struct EspCloud : pocket::PlatformCloud {
  std::string create_pair_session(const std::string&) override { return "AAAAAAAA"; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(pocket::DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
};

struct EspDisplay : pocket::PlatformDisplay {
  pocket::board::EpdDisplay& epd;
  explicit EspDisplay(pocket::board::EpdDisplay& e) : epd(e) {}
  void present(const pocket::Canvas& c, pocket::RefreshMode mode) override {
    epd.present(c, mode);
  }
};

}  // namespace

extern "C" void app_main(void) {
  // Breadcrumb ASAP — prove we passed cpu_start before any e-ink I/O.
  const esp_reset_reason_t rr = esp_reset_reason();
  ESP_LOGI(TAG, "app_main start reset=%s (%d)", reset_reason_str(rr), static_cast<int>(rr));
  vTaskDelay(pdMS_TO_TICKS(50));  // let USB-Serial/JTAG flush

  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Pocket boot — canvas %dx%d, pins Up=%d Fn=%d Down=%d BOOT=%d PWR=%d",
           pocket::board::kLogicalW, pocket::board::kLogicalH, pocket::board::kPinButtonUp,
           pocket::board::kPinButtonFunction, pocket::board::kPinButtonDown, pocket::board::kPinBoot,
           pocket::board::kPinPwr);

  // Buttons first so we always reach the input loop after present returns.
  static pocket::board::ButtonPoller buttons;
  buttons.init();

  // EPD_VCC is supplied by AXP2101 ALDOs on this Waveshare board.
  ESP_LOGI(TAG, "enabling AXP EPD rails…");
  pocket::board::axp_enable_epd_rails();

  static pocket::board::EpdDisplay epd;
  ESP_LOGI(TAG, "e-paper init…");
  if (!epd.init()) {
    ESP_LOGE(TAG, "e-paper init failed — continuing headless");
  }

  static pocket::MemoryConfigStore store;
  static EspClock clock;
  static EspWifi wifi;
  static EspCloud cloud;
  static EspDisplay display(epd);
  static pocket::InputMapper mapper;

  pocket::App app(store, clock, wifi, cloud, display);
  ESP_LOGI(TAG, "painting first frame (clears factory demo if panel responds)…");
  app.boot();
  ESP_LOGI(TAG, "UI boot complete, screen=%d — entering input loop", static_cast<int>(app.screen()));

  while (true) {
    esp_task_wdt_reset();
    const uint32_t now = clock.now_ms();
    buttons.poll(mapper, now);
    for (;;) {
      const pocket::InputEvent e = mapper.poll();
      if (e == pocket::InputEvent::None) break;
      app.handle(e);
    }
    app.tick(now);
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
