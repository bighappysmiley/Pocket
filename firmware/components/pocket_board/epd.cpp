#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_epd";
constexpr size_t kPanel2bppBytes = (kPanelW * kPanelH) / 4;  // 96000
constexpr size_t kPanel1bppBytes = (kPanelW * kPanelH) / 8;  // 48000

inline spi_device_handle_t as_spi(void* p) { return static_cast<spi_device_handle_t>(p); }

}  // namespace

bool EpdDisplay::init() {
  if (ready_) return true;

  panel_2bpp_ = static_cast<uint8_t*>(
      heap_caps_malloc(kPanel2bppBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  panel_1bpp_ = static_cast<uint8_t*>(
      heap_caps_malloc(kPanel1bppBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!panel_2bpp_ || !panel_1bpp_) {
    ESP_LOGE(TAG, "SPIRAM alloc failed for e-paper framebuffers");
    return false;
  }

  spi_init();
  gpio_init();
  // Match Waveshare demo: CS held low for the whole session (single device).
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 1);

  ready_ = true;
  ESP_LOGI(TAG, "e-paper ready (SPI3, CS=%d DC=%d RST=%d BUSY=%d)", kPinEpdCs, kPinEpdDc, kPinEpdRst,
           kPinEpdBusy);
  return true;
}

void EpdDisplay::gpio_init() {
  gpio_config_t out = {};
  out.intr_type = GPIO_INTR_DISABLE;
  out.mode = GPIO_MODE_OUTPUT;
  out.pin_bit_mask = (1ULL << kPinEpdRst) | (1ULL << kPinEpdDc) | (1ULL << kPinEpdCs);
  out.pull_down_en = GPIO_PULLDOWN_DISABLE;
  out.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&out));

  gpio_config_t in = {};
  in.intr_type = GPIO_INTR_DISABLE;
  in.mode = GPIO_MODE_INPUT;
  in.pin_bit_mask = (1ULL << kPinEpdBusy);
  in.pull_down_en = GPIO_PULLDOWN_DISABLE;
  in.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&in));
}

void EpdDisplay::spi_init() {
  spi_bus_config_t bus = {};
  bus.miso_io_num = -1;
  bus.mosi_io_num = kPinEpdMosi;
  bus.sclk_io_num = kPinEpdSclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = 65536;

  spi_device_interface_config_t dev = {};
  dev.spics_io_num = -1;  // software CS (held low)
  dev.clock_speed_hz = 20 * 1000 * 1000;
  dev.mode = 0;
  dev.queue_size = 1;

  ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &bus, SPI_DMA_CH_AUTO));
  spi_device_handle_t handle = nullptr;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI3_HOST, &dev, &handle));
  spi_ = handle;
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
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 0);
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &cmd;
  ESP_ERROR_CHECK(spi_device_polling_transmit(as_spi(spi_), &t));
}

void EpdDisplay::send_data(uint8_t data) {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 1);
  spi_transaction_t t = {};
  t.length = 8;
  t.tx_buffer = &data;
  ESP_ERROR_CHECK(spi_device_polling_transmit(as_spi(spi_), &t));
}

void EpdDisplay::send_buffer(const uint8_t* data, size_t len) {
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 1);
  constexpr size_t kChunk = 4096;
  for (size_t i = 0; i < len; i += kChunk) {
    const size_t n = (i + kChunk > len) ? (len - i) : kChunk;
    spi_transaction_t t = {};
    t.length = n * 8;
    t.tx_buffer = data + i;
    ESP_ERROR_CHECK(spi_device_polling_transmit(as_spi(spi_), &t));
  }
}

void EpdDisplay::wait_busy() {
  vTaskDelay(pdMS_TO_TICKS(20));
  while (gpio_get_level(static_cast<gpio_num_t>(kPinEpdBusy)) == 1) {
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void EpdDisplay::turn_on_full() {
  send_cmd(0x22);
  send_data(0xF7);
  send_cmd(0x20);
  wait_busy();
}

void EpdDisplay::turn_on_4gray() {
  send_cmd(0x22);
  send_data(0xD7);
  send_cmd(0x20);
  wait_busy();
}

void EpdDisplay::turn_on_fast() {
  send_cmd(0x22);
  send_data(0xD7);
  send_cmd(0x20);
  wait_busy();
}

void EpdDisplay::init_4gray() {
  // Adapted from Waveshare ESP-IDF EPD_Init_4GRAY (MIT).
  vTaskDelay(pdMS_TO_TICKS(50));
  reset();
  wait_busy();
  send_cmd(0x12);  // SWRESET
  wait_busy();

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
  wait_busy();

  send_cmd(0x3C);
  send_data(0x01);

  send_cmd(0x18);
  send_data(0x80);

  send_cmd(0x1A);
  send_data(0x5A);
}

void EpdDisplay::init_fast() {
  vTaskDelay(pdMS_TO_TICKS(50));
  reset();
  wait_busy();
  send_cmd(0x12);
  wait_busy();

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
  wait_busy();

  send_cmd(0x3C);
  send_data(0x01);

  send_cmd(0x18);
  send_data(0x80);

  send_cmd(0x1A);
  send_data(0x6A);
}

void EpdDisplay::display_4gray(const uint8_t* image) {
  // Waveshare EPD_Display_4Gray bit-plane packing (MIT).
  const size_t mono_bytes = kPanel1bppBytes;
  send_cmd(0x24);
  for (size_t i = 0; i < mono_bytes; ++i) {
    uint8_t temp3 = 0;
    for (int j = 0; j < 2; ++j) {
      uint8_t temp1 = image[i * 2 + static_cast<size_t>(j)];
      for (int k = 0; k < 2; ++k) {
        uint8_t temp2 = temp1 & 0xC0;
        if (temp2 == 0xC0)
          temp3 |= 0x00;
        else if (temp2 == 0x00)
          temp3 |= 0x01;
        else if (temp2 == 0x80)
          temp3 |= 0x01;
        else
          temp3 |= 0x00;
        temp3 <<= 1;
        temp1 <<= 2;
        temp2 = temp1 & 0xC0;
        if (temp2 == 0xC0)
          temp3 |= 0x00;
        else if (temp2 == 0x00)
          temp3 |= 0x01;
        else if (temp2 == 0x80)
          temp3 |= 0x01;
        else
          temp3 |= 0x00;
        if (!(j == 1 && k == 1)) temp3 <<= 1;
        temp1 <<= 2;
      }
    }
    send_data(temp3);
  }

  send_cmd(0x26);
  for (size_t i = 0; i < mono_bytes; ++i) {
    uint8_t temp3 = 0;
    for (int j = 0; j < 2; ++j) {
      uint8_t temp1 = image[i * 2 + static_cast<size_t>(j)];
      for (int k = 0; k < 2; ++k) {
        uint8_t temp2 = temp1 & 0xC0;
        if (temp2 == 0xC0)
          temp3 |= 0x00;
        else if (temp2 == 0x00)
          temp3 |= 0x01;
        else if (temp2 == 0x80)
          temp3 |= 0x00;
        else
          temp3 |= 0x01;
        temp3 <<= 1;
        temp1 <<= 2;
        temp2 = temp1 & 0xC0;
        if (temp2 == 0xC0)
          temp3 |= 0x00;
        else if (temp2 == 0x00)
          temp3 |= 0x01;
        else if (temp2 == 0x80)
          temp3 |= 0x00;
        else
          temp3 |= 0x01;
        if (!(j == 1 && k == 1)) temp3 <<= 1;
        temp1 <<= 2;
      }
    }
    send_data(temp3);
  }
  turn_on_4gray();
}

void EpdDisplay::display_mono_fast(const uint8_t* image_1bpp) {
  send_cmd(0x24);
  send_buffer(image_1bpp, kPanel1bppBytes);
  turn_on_fast();
}

void EpdDisplay::rotate_to_panel_2bpp(const pocket::Canvas& src, uint8_t* dst) {
  // Logical portrait (x,y) 480×800 → panel landscape (px,py) 800×480 via 90° CW:
  //   px = y,  py = (W-1) - x
  std::memset(dst, 0xFF, kPanel2bppBytes);  // white default
  for (int y = 0; y < kLogicalH; ++y) {
    for (int x = 0; x < kLogicalW; ++x) {
      const int px = y;
      const int py = (kLogicalW - 1) - x;
      const Gray g = src.get_pixel(x, y);
      const size_t i = static_cast<size_t>(py * kPanelW + px);
      const size_t bi = i / 4;
      const int shift = 6 - static_cast<int>((i % 4) * 2);
      dst[bi] = static_cast<uint8_t>((dst[bi] & ~(0x3 << shift)) |
                                     ((static_cast<uint8_t>(g) & 0x3) << shift));
    }
  }
}

void EpdDisplay::panel_2bpp_to_mono(const uint8_t* src, uint8_t* dst) {
  // Threshold: G0/G1 → black (0), G2/G3 → white (1) in 1bpp MSB-first.
  for (int y = 0; y < kPanelH; ++y) {
    for (int x = 0; x < kPanelW; x += 8) {
      uint8_t byte = 0;
      for (int b = 0; b < 8; ++b) {
        const size_t i = static_cast<size_t>(y * kPanelW + x + b);
        const size_t bi = i / 4;
        const int shift = 6 - static_cast<int>((i % 4) * 2);
        const uint8_t g = (src[bi] >> shift) & 0x3;
        if (g >= 2) byte |= static_cast<uint8_t>(0x80 >> b);
      }
      dst[static_cast<size_t>(y * (kPanelW / 8) + (x / 8))] = byte;
    }
  }
}

void EpdDisplay::present(const pocket::Canvas& canvas, pocket::RefreshMode mode) {
  if (!ready_ && !init()) return;
  rotate_to_panel_2bpp(canvas, panel_2bpp_);

  if (mode == pocket::RefreshMode::Full) {
    ESP_LOGI(TAG, "full 4-gray refresh");
    init_4gray();
    display_4gray(panel_2bpp_);
  } else {
    ESP_LOGI(TAG, "fast mono refresh");
    panel_2bpp_to_mono(panel_2bpp_, panel_1bpp_);
    init_fast();
    display_mono_fast(panel_1bpp_);
  }
}

void EpdDisplay::sleep() {
  if (!ready_) return;
  send_cmd(0x10);
  send_data(0x01);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdRst), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdCs), 0);
  gpio_set_level(static_cast<gpio_num_t>(kPinEpdDc), 0);
}

}  // namespace pocket::board
