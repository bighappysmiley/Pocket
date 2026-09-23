#pragma once
#include "pocket/canvas.hpp"
#include "pocket/refresh.hpp"
#include <cstdint>

namespace pocket::board {

/** Waveshare 3.97" e-Paper (SSD1683-class) on ESP32-S3-ePaper-3.97. */
class EpdDisplay {
 public:
  /** Init SPI + GPIO, HW reset, and panel registers. Call once at boot. */
  bool init();

  /**
   * Present Pocket's 480×800 2bpp canvas: rotate 90° CW onto 800×480 panel
   * and force a full 4-gray refresh (clears any latched factory demo image).
   */
  void present(const pocket::Canvas& canvas, pocket::RefreshMode mode);

  void sleep();

 private:
  void gpio_init();
  void spi_init();
  void reset();
  void send_cmd(uint8_t cmd);
  void send_data(uint8_t data);
  void send_buffer(const uint8_t* data, size_t len);
  void wait_busy();
  void turn_on_full();
  void turn_on_4gray();
  void turn_on_fast();
  void init_4gray();
  void init_fast();
  void display_4gray(const uint8_t* image_2bpp);
  void display_mono_fast(const uint8_t* image_1bpp);

  static void rotate_to_panel_2bpp(const pocket::Canvas& src, uint8_t* dst);
  static void panel_2bpp_to_mono(const uint8_t* src_2bpp, uint8_t* dst_1bpp);

  bool ready_ = false;
  void* spi_ = nullptr;  // spi_device_handle_t
  uint8_t* panel_2bpp_ = nullptr;
  uint8_t* panel_1bpp_ = nullptr;
};

}  // namespace pocket::board
