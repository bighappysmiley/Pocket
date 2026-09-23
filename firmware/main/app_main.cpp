#include <stdio.h>
#include "pocket/app.hpp"
#include "pocket/input.hpp"

// ESP-IDF entry — board bring-up hooks. Host builds use firmware/host instead.
#ifdef POCKET_HOST
#error "app_main is for ESP-IDF only"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

static const char* TAG = "pocket";

extern "C" void app_main(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  ESP_LOGI(TAG, "Pocket firmware boot — canvas 480x800, controls per hardware-controls-v1.2");
  // Board bring-up (display rotation, Button_Up/Down/Function, BOOT, PWR, ES8311)
  // is wired in board/ when Waveshare BSP is linked. UI shell lives in pocket_ui.
  // Until BSP is attached, run a headless App tick for smoke validation.
  static pocket::MemoryConfigStore store;
  struct EspClock : pocket::PlatformClock {
    uint32_t now_ms() override { return static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS); }
    void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
      hour = 12;
      minute = 0;
      weekday = 0;
      month = 0;
      day = 1;
    }
  } clock;
  struct EspWifi : pocket::PlatformWifi {
    std::vector<std::string> scan() override { return {}; }
    bool connect(const std::string&, const std::string&) override { return false; }
    bool connected() const override { return false; }
  } wifi;
  struct EspCloud : pocket::PlatformCloud {
    std::string create_pair_session(const std::string&) override { return "AAAAAAAA"; }
    std::string pair_status(const std::string&) override { return "pending"; }
    void refresh_entitlement(pocket::DeviceConfig&) override {}
    std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
  } cloud;
  struct EspDisplay : pocket::PlatformDisplay {
    void present(const pocket::Canvas&, pocket::RefreshMode) override {}
  } display;

  pocket::App app(store, clock, wifi, cloud, display);
  app.boot();
  ESP_LOGI(TAG, "UI boot complete, screen=%d", static_cast<int>(app.screen()));

  while (true) {
    app.tick(clock.now_ms());
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}