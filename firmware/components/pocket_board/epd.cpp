#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_epd";
constexpr size_t kPanel1bppBytes = (kPanelW * kPanelH) / 8;  // 48000
constexpr uint32_t kBusyTimeoutMs = 8000;

inline spi_device_handle_t as_spi(void* p) { return static_cast<spi_device_handle_t>(p); }

inline void wdt_kick() {
  // Safe if WDT not subscribed to this task.
  esp_task_wdt_reset();
}

uint8_t* alloc_fb(size_t n) {
  uint8_t* p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!p) p = static_cast<uint8_t*>(heap_caps_malloc(n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
  if (!p) p = static_cast<uint8_t*>(malloc(n));
  return p;
}

}  // namespace

bool EpdDisplay::init() {
  if (ready_) return true;

  panel_1bpp_ = alloc_fb(kPanel1bppBytes);
  if (!panel_1bpp_) {
    ESP_LOGE(TAG, "framebuffer alloc failed (%u bytes)", (unsigned)kPanel1bppBytes);
    return false;
  }

  if (!spi_init()) return false;
  gpio_init();
  // Waveshare demo holds CS low for the session (single device on the bus).
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 1);

  ready_ = true;
  ESP_LOGI(TAG, "e-paper ready SPI3 CS=%d DC=%d RST=%d BUSY=%d", kPinEpdCs, kPinEpdDc, kPinEpdRst,
           kPinEpdBusy);
  return true;
}

void EpdDisplay::gpio_init() {
  gpio_config_t out = {};
  out.intr_type = GPIO_INTR_DISABLE;
  out.mode = GPIO_MODE_OUTPUT;
  out.pin_bit_mask = (1ULL << kPinEpdRst) | (1ULL << kPinEpdDc) | (1ULL << kPinEpdCs);
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.pull_up_en = GPIO_PULLUP_DISABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&out));

  // BUSY: HIGH while busy (Waveshare). No pull-up — a floating high would fake "forever busy".
  gpio_config_t in = {};
  in.intr_type = GPIO_INTR_DISABLE;
  in.mode = GPIO_MODE_INPUT;
  in.pin_bit_mask = (1ULL << kPinEpdBusy);
  in.pull_down_en = GPIO_PULLDOWN_DISABLE;
  in.pull_up_en = GPIO_PULLUP_DISABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&in));
}

bool EpdDisplay::spi_init() {
  spi_bus_config_t bus = {};
  bus.miso_io_num = -1;
  bus.mosi_io_num = kPinEpdMosi;
  bus.sclk_io_num = kPinEpdSclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 65536;

  spi_device_interface_config_t dev = {};
  dev.spics_io_num = -1;  // software CS
  dev.clock_speed_hz = 20 * 1000 * 1000;
  dev.mode = 0;
  dev.queue_size = 1;

  esp_err_t err = spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
    return false;
  }
  spi_device_handle_t handle = nullptr;
  err = spi_bus_add_device(SPI3_HOST, &dev, &handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
    return false;
  }
  spi_ = handle;
  return true;
}

void EpdDisplay::cs(bool level) {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), level ? 1 : 0);
}

void EpdDisplay::reset() {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 1);
  vTaskDelay(pdMS_TO_TICKS(50));
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 0);
  vTaskDelay(pdMS_TO_TICKS(2));
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 1);
  vTaskDelay(pdMS_TO_TICKS(50));
}

void EpdDisplay::send_cmd(uint8_t cmd) {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 0);
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &cmd;
  spi_device_polling_transmit(as_spi(spi_), &t);
}

void EpdDisplay::send_data(uint8_t data) {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 1);
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &data;
  spi_device_polling_transmit(as_spi(spi_), &t);
}

void EpdDisplay::send_buffer(const uint8_t* data, size_t len) {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 1);
  constexpr size_t kChunk = 4096;
  for (size_t i = 0; i < len; i += kChunk) {
    wdt_kick();
    const size_t n = (i + kChunk > len) ? (len - i) : kChunk;
    spi_transaction_t t = {};
    t.length = n * 8;
    t.tx_buffer = data + i;
    spi_device_polling_transmit(as_spi(spi_), &t);
  }
}

bool EpdDisplay::wait_busy(uint32_t timeout_ms) {
  const TickType_t start = xTaskGetTickCount();
  const TickType_t limit = pdMS_TO_TICKS(timeout_ms);
  vTaskDelay(pdMS_TO_TICKS(10));
  while (gpio_get_level(static_cast<gpio_num_t>(kPinEpdBusy)) == 1) {
    wdt_kick();
    if ((xTaskGetTickCount() - start) > limit) {
      ESP_LOGW(TAG, "BUSY timeout after %lums (pin stuck high?)", (unsigned long)timeout_ms);
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
  return true;
}

void EpdDisplay::turn_on_full() {
  send_cmd(0x22);
  send_data(0xF7);
  send_cmd(0x20);
  wait_busy(kBusyTimeoutMs);
}

void EpdDisplay::turn_on_fast() {
  send_cmd(0x22);
  send_data(0xD7);
  send_cmd(0x20);
  wait_busy(kBusyTimeoutMs);
}

bool EpdDisplay::init_full() {
  // Waveshare EPD_Init (mono full) — MIT demo.
  wdt_kick();
  vTaskDelay(pdMS_TO_TICKS(10));
  reset();
  if (!wait_busy(kBusyTimeoutMs)) return false;
  send_cmd(0x12);  // SWRESET
  if (!wait_busy(kBusyTimeoutMs)) return false;

  send_cmd(0x18);
  send_data(0x80);

  send_cmd(0x0C);
  send_data(0xAE);
  send_data(0xC7);
  send_data(0xC3);
  send_data(0xC0);
  send_data(0x80);

  send_cmd(0x01);
  send_data(static_cast<uint8_t>((kPanelH - 1) % 256));
  send_data(static_cast<uint8_t>((kPanelH - 1) / 256));
  send_data(0x02);

  send_cmd(0x3C);
  send_data(0x01);

  send_cmd(0x11);
  send_data(0x01);

  send_cmd(0x44);
  send_data(0x00);
  send_data(0x00);
  send_data(static_cast<uint8_t>((kPanelW - 1) % 256));
  send_data(static_cast<uint8_t>((kPanelW - 1) / 256));

  send_cmd(0x45);
  send_data(static_cast<uint8_t>((kPanelH - 1) % 256));
  send_data(static_cast<uint8_t>((kPanelH - 1) / 256));
  send_data(0x00);
  send_data(0x00);

  send_cmd(0x4E);
  send_data(0x00);
  send_data(0x00);
  send_cmd(0x4F);
  send_data(0x00);
  send_data(0x00);
  return wait_busy(kBusyTimeoutMs);
}

bool EpdDisplay::init_fast() {
  wdt_kick();
  vTaskDelay(pdMS_TO_TICKS(50));
  reset();
  if (!wait_busy(kBusyTimeoutMs)) return false;
  send_cmd(0x12);
  if (!wait_busy(kBusyTimeoutMs)) return false;

  send_cmd(0x0C);
  send_data(0xAE);
  send_data(0xC7);
  send_data(0xC3);
  send_data(0xC0);
  send_data(0x80);

  send_cmd(0x01);
  send_data(static_cast<uint8_t>((kPanelH - 1) % 256));
  send_data(static_cast<uint8_t>((kPanelH - 1) / 256));
  send_data(0x02);

  send_cmd(0x11);
  send_data(0x01);

  send_cmd(0x44);
  send_data(0x00);
  send_data(0x00);
  send_data(static_cast<uint8_t>((kPanelW - 1) % 256));
  send_data(static_cast<uint8_t>((kPanelW - 1) / 256));

  send_cmd(0x45);
  send_data(static_cast<uint8_t>((kPanelH - 1) % 256));
  send_data(static_cast<uint8_t>((kPanelH - 1) / 256));
  send_data(0x00);
  send_data(0x00);

  send_cmd(0x4E);
  send_data(0x00);
  send_data(0x00);
  send_cmd(0x4F);
  send_data(0x00);
  send_data(0x00);
  if (!wait_busy(kBusyTimeoutMs)) return false;

  send_cmd(0x3C);
  send_data(0x01);

  send_cmd(0x18);
  send_data(0x80);

  send_cmd(0x1A);
  send_data(0x6A);
  return true;
}

void EpdDisplay::display_full(const uint8_t* mono) {
  send_cmd(0x24);
  send_buffer(mono, kPanel1bppBytes);
  // Also write RED/OLD RAM so partial diffs have a clean base (Waveshare Display_Base).
  send_cmd(0x26);
  send_buffer(mono, kPanel1bppBytes);
  turn_on_full();
}

void EpdDisplay::display_fast(const uint8_t* mono) {
  send_cmd(0x24);
  send_buffer(mono, kPanel1bppBytes);
  turn_on_fast();
}

void EpdDisplay::rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst) {
  // 90° CW: panel(px,py) ← logical(x,y) with px=y, py=(W-1)-x
  // Inverse: x=(W-1)-py, y=px. Threshold G0/G1→black(0), G2/G3→white(1).
  std::memset(dst, 0xFF, kPanel1bppBytes);
  for (int py = 0; py < kPanelH; ++py) {
    if ((py & 31) == 0) wdt_kick();
    for (int px = 0; px < kPanelW; px += 8) {
      uint8_t byte = 0;
      for (int b = 0; b < 8; ++b) {
        const int x = (kLogicalW - 1) - py;
        const int y = px + b;
        const Gray g = src.get_pixel(x, y);
        if (static_cast<uint8_t>(g) >= 2) byte |= static_cast<uint8_t>(0x80 >> b);
      }
      dst[static_cast<size_t>(py * (kPanelW / 8) + (px / 8))] = byte;
    }
  }
}

void EpdDisplay::present(const pocket::Canvas& canvas, pocket::RefreshMode mode) {
  if (!ready_ && !init()) {
    ESP_LOGE(TAG, "present skipped — not ready");
    return;
  }
  wdt_kick();
  rotate_canvas_to_mono(canvas, panel_1bpp_);
  wdt_kick();

  const bool full = (mode == pocket::RefreshMode::Full);
  ESP_LOGI(TAG, "%s mono refresh", full ? "full" : "fast");
  bool ok = full ? init_full() : init_fast();
  if (!ok) {
    ESP_LOGE(TAG, "panel init failed — leaving previous image");
    return;
  }
  if (full) {
    display_full(panel_1bpp_);
  } else {
    display_fast(panel_1bpp_);
  }
  ESP_LOGI(TAG, "refresh done");
}

void EpdDisplay::sleep() {
  if (!ready_) return;
  send_cmd(0x10);
  send_data(0x01);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 0);
  cs(false);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 0);
}

}  // namespace pocket::board
