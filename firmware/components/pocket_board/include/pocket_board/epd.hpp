#pragma once
#include "pocket/canvas.hpp"
#include "pocket/refresh.hpp"
#include <cstdint>

namespace pocket::board {

/**
 * Waveshare 3.97" e-Paper on ESP32-S3-ePaper-3.97 (SSD1683-class).
 *
 * Uses mono full/fast refresh with bulk SPI (not per-byte 4-gray packing) so
 * boot cannot trip the task WDT or hang forever on BUSY before the input loop.
 */
class EpdDisplay {
 public:
  bool init();
  void present(const pocket::Canvas& canvas, pocket::RefreshMode mode);
  void sleep();

 private:
  void gpio_init();
  bool spi_init();
  void reset();
  void cs(bool level);
  void send_cmd(uint8_t cmd);
  void send_data(uint8_t data);
  void send_buffer(const uint8_t* data, size_t len);
  bool wait_busy(uint32_t timeout_ms);
  void turn_on_full();
  void turn_on_fast();
  bool init_full();
  bool init_fast();
  void display_full(const uint8_t* mono);
  void display_fast(const uint8_t* mono);
  void rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst);

  bool ready_ = false;
  void* spi_ = nullptr;
  uint8_t* panel_1bpp_ = nullptr;
};

}  // namespace pocket::board
