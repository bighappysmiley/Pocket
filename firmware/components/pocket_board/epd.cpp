#include "pocket_board/epd.hpp"
#include "pocket_board/pins.hpp"
#include "waveshare_epd.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>

// Waveshare typedef used by EPD_Display_Partial
using UWORD = uint16_t;

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

}  // namespace

bool EpdDisplay::init() {
  if (ready_) return true;

  panel_1bpp_ = alloc_fb(kMonoBytes);
  if (!panel_1bpp_) {
    ESP_LOGE(TAG, "framebuffer alloc failed");
    return false;
  }

  epaper_port_init();
  // Waveshare: CS already held low inside epaper_gpio_Init.

  ESP_LOGI(TAG, "wiping factory image (bulk white refresh)…");
  EPD_Init();
  std::memset(panel_1bpp_, 0xFF, kMonoBytes);
  EPD_Display_Base(panel_1bpp_);
  ESP_LOGI(TAG, "factory wipe done");

  ready_ = true;
  return true;
}

void EpdDisplay::rotate_canvas_to_mono(const pocket::Canvas& src, uint8_t* dst) {
  std::memset(dst, 0xFF, kMonoBytes);
  for (int py = 0; py < kPanelH; ++py) {
    for (int px = 0; px < kPanelW; px += 8) {
      uint8_t byte = 0;
      for (int b = 0; b < 8; ++b) {
        const int x = (kLogicalW - 1) - py;
        const int y = px + b;
        if (static_cast<uint8_t>(src.get_pixel(x, y)) >= gray_threshold_) {
          byte |= static_cast<uint8_t>(0x80 >> b);
        }
      }
      dst[static_cast<size_t>(py * (kPanelW / 8) + (px / 8))] = byte;
    }
    if ((py & 127) == 0) taskYIELD();
  }
}

void EpdDisplay::rotate_canvas_to_mono_region(const pocket::Canvas& src, uint8_t* dst, int px0,
                                              int px1, int py0, int py1) {
  const int byte_w = (px1 - px0) / 8;
  for (int py = py0; py < py1; ++py) {
    const int x = (kLogicalW - 1) - py;
    for (int pxb = 0; pxb < byte_w; ++pxb) {
      const int px = px0 + pxb * 8;
      uint8_t byte = 0;
      for (int b = 0; b < 8; ++b) {
        const int y = px + b;
        if (static_cast<uint8_t>(src.get_pixel(x, y)) >= gray_threshold_) {
          byte |= static_cast<uint8_t>(0x80 >> b);
        }
      }
      dst[static_cast<size_t>(py - py0) * static_cast<size_t>(byte_w) + static_cast<size_t>(pxb)] =
          byte;
    }
  }
}

void EpdDisplay::present(const pocket::Canvas& canvas, pocket::RefreshMode mode) {
  if (!ready_ && !init()) return;
  rotate_canvas_to_mono(canvas, panel_1bpp_);

  // Ghosting / screen-enter "full" uses the panel's fastest full LUT (0xD7, ~1.5 s),
  // not the slow OTP 0xF7 (~3.5 s). Factory wipe in init() still uses 0xF7 once.
  ESP_LOGI(TAG, "Waveshare fast full LUT (0xD7) mode=%s",
           mode == pocket::RefreshMode::Full ? "full" : "partial-canvas");
  EPD_Init_Fast();
  EPD_Display_Fast_Base(panel_1bpp_);
  ESP_LOGI(TAG, "refresh done");
}

void EpdDisplay::present_region(const pocket::Canvas& canvas, int lx, int ly, int lw, int lh) {
  if (!ready_ && !init()) return;
  if (lw <= 0 || lh <= 0) {
    present(canvas, pocket::RefreshMode::Partial);
    return;
  }
  // Clamp logical rect.
  if (lx < 0) {
    lw += lx;
    lx = 0;
  }
  if (ly < 0) {
    lh += ly;
    ly = 0;
  }
  if (lx + lw > kLogicalW) lw = kLogicalW - lx;
  if (ly + lh > kLogicalH) lh = kLogicalH - ly;
  if (lw <= 0 || lh <= 0) return;

  // Logical portrait (lx,ly) → panel landscape: px=ly, py=(kLogicalW-1)-lx
  // Region maps to panel x ∈ [ly, ly+lh), panel y ∈ [kLogicalW-(lx+lw), kLogicalW-lx)
  int px0 = ly;
  int px1 = ly + lh;
  int py0 = kLogicalW - (lx + lw);
  int py1 = kLogicalW - lx;
  if (px0 < 0) px0 = 0;
  if (py0 < 0) py0 = 0;
  if (px1 > kPanelW) px1 = kPanelW;
  if (py1 > kPanelH) py1 = kPanelH;
  // Align X to byte boundary for the controller.
  px0 = (px0 / 8) * 8;
  px1 = ((px1 + 7) / 8) * 8;
  if (px1 > kPanelW) px1 = kPanelW;
  if (px1 <= px0 || py1 <= py0) {
    present(canvas, pocket::RefreshMode::Partial);
    return;
  }

  const int byte_w = (px1 - px0) / 8;
  const int rows = py1 - py0;
  if (byte_w <= 0 || rows <= 0) {
    present(canvas, pocket::RefreshMode::Partial);
    return;
  }

  // Waveshare EPD_Display_Partial expects a tightly packed region buffer
  // (window width × height), not the full framebuffer from offset 0. Rotate
  // only this window directly — a status-bar tick or a PIN digit no longer
  // pays for re-scanning the full 480×800 canvas just to keep a small strip.
  const size_t region_bytes = static_cast<size_t>(byte_w) * static_cast<size_t>(rows);
  uint8_t* region = static_cast<uint8_t*>(malloc(region_bytes));
  if (!region) {
    present(canvas, pocket::RefreshMode::Partial);
    return;
  }
  rotate_canvas_to_mono_region(canvas, region, px0, px1, py0, py1);

  ESP_LOGI(TAG, "Waveshare region partial px=%d..%d py=%d..%d bytes=%u", px0, px1, py0, py1,
           static_cast<unsigned>(region_bytes));
  EPD_Display_Partial(region, static_cast<UWORD>(px0), static_cast<UWORD>(py0),
                      static_cast<UWORD>(px1), static_cast<UWORD>(py1));
  free(region);
  ESP_LOGI(TAG, "region refresh done");
}

void EpdDisplay::set_brightness(int percent) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  // 3 real steps (Gray has 4 levels, 0..3) — below/around/above the historical default.
  if (percent < 34) {
    gray_threshold_ = 1;  // darker: only near-white counts as white
  } else if (percent < 67) {
    gray_threshold_ = 2;  // previous fixed behavior
  } else {
    gray_threshold_ = 3;  // lighter: only near-black counts as black
  }
}

void EpdDisplay::sleep() {
  if (!ready_) return;
  EPD_Sleep();
}

}  // namespace pocket::board
