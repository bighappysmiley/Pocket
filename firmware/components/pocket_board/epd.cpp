#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"
#include "waveshare_epd.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_epd";
constexpr size_t kMonoBytes = EPD_SIZE_MONO;

uint8_t* alloc_fb(size_t n) {
  uint8_t* p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!p) p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!p) p = static_cast<uint8_t*>(malloc(n));
  return p;
}

// Fully stop TWDT for long e-ink I/O. Deleting only the main task still leaves
// idle-task checks armed; busy SPI without yields then reboots the chip.
struct WdtPause {
  bool stopped = false;
  WdtPause() {
    if (esp_task_wdt_deinit() == ESP_OK) stopped = true;
  }
  ~WdtPause() {
    if (!stopped) return;
    esp_task_wdt_config_t cfg = {
        .timeout_ms = 60000,
        .idle_core_mask = 0,
        .trigger_panic = true,
    };
    if (esp_task_wdt_init(&cfg) == ESP_OK) {
      esp_task_wdt_add(xTaskGetCurrentTaskHandle());
    }
  }
};

}  // namespace

bool EpdDisplay::init() {
  if (ready_) return true;

  panel_1bpp_ = alloc_fb(kMonoBytes);
  if (!panel_1bpp_) {
    ESP_LOGE(TAG, "framebuffer alloc failed");
    return false;
  }

  epaper_port_init();
  // Soft-CS is handled inside the vendor driver; leave CS idle-high.

  ESP_LOGI(TAG, "wiping factory image (bulk white refresh)…");
  {
    WdtPause pause;
    EPD_Init();
    std::memset(panel_1bpp_, 0xFF, kMonoBytes);
    EPD_Display_Base(panel_1bpp_);
  }
  ESP_LOGI(TAG, "factory wipe done");

  ready_ = true;
  return true;
}

void EpdDisplay::rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst) {
  std::memset(dst, 0xFF, kMonoBytes);
  for (int py = 0; py < kPanelH; ++py) {
    if ((py & 31) == 0) esp_task_wdt_reset();
    for (int px = 0; px < kPanelW; px += 8) {
      uint8_t byte = 0;
      for (int b = 0; b < 8; ++b) {
        const int x = (kLogicalW - 1) - py;
        const int y = px + b;
        if (static_cast<uint8_t>(src.get_pixel(x, y)) >= 2) {
          byte |= static_cast<uint8_t>(0x80 >> b);
        }
      }
      dst[static_cast<size_t>(py * (kPanelW / 8) + (px / 8))] = byte;
    }
  }
}

void EpdDisplay::present(const pocket::Canvas& canvas, pocket::RefreshMode mode) {
  if (!ready_ && !init()) return;
  esp_task_wdt_reset();
  rotate_canvas_to_mono(canvas, panel_1bpp_);

  WdtPause pause;
  if (mode == pocket::RefreshMode::Full) {
    ESP_LOGI(TAG, "Waveshare full refresh");
    EPD_Init();
    EPD_Display_Base(panel_1bpp_);
  } else {
    ESP_LOGI(TAG, "Waveshare fast refresh");
    EPD_Init_Fast();
    EPD_Display_Fast(panel_1bpp_);
  }
  ESP_LOGI(TAG, "refresh done");
}

void EpdDisplay::sleep() {
  if (!ready_) return;
  EPD_Sleep();
}

}  // namespace pocket::board
