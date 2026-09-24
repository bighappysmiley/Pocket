#pragma once
/**
 * Waveshare ESP32-S3-ePaper-3.97 — Pocket pin map (Part C).
 *
 * Source of truth for GPIO numbers:
 *   - Official demo `button_bsp.c` (waveshareteam/ESP32-S3-ePaper-3.97)
 *     Button_Up=GPIO4, Button_Function=GPIO5, Button_Down=GPIO6, Boot=GPIO0
 *   - Official `epaper_port.h` EPD SPI: SCLK=11 MOSI=12 CS=10 DC=9 RST=46 BUSY=3
 *   - `pcf85063_bsp.h` PWR_OUT_PIN = GPIO1 (active-low)
 *
 * No volume keys.
 */
namespace pocket::board {

// --- Rotary 3-way (active-low, internal pull-up) ---
constexpr int kPinButtonUp = 4;        // Schematic net: Button_Up
constexpr int kPinButtonDown = 6;      // Schematic net: Button_Down
constexpr int kPinButtonFunction = 5;  // Schematic net: Button_Function

// --- Side buttons ---
constexpr int kPinBoot = 0;  // ESP32-S3 BOOT / download mode
constexpr int kPinPwr = 1;   // PWR — active-low when pressed

// --- 3.97" e-Paper SPI (800×480 native) ---
constexpr int kPinEpdSclk = 11;
constexpr int kPinEpdMosi = 12;
constexpr int kPinEpdCs = 10;
constexpr int kPinEpdDc = 9;
constexpr int kPinEpdRst = 46;
constexpr int kPinEpdBusy = 3;  // HIGH = busy

constexpr int kPanelW = 800;
constexpr int kPanelH = 480;
constexpr int kLogicalW = 480;
constexpr int kLogicalH = 800;

// --- microSD (SDMMC 4-bit, Waveshare demo pins) ---
constexpr int kPinSdClk = 16;
constexpr int kPinSdCmd = 17;
constexpr int kPinSdD0 = 15;
constexpr int kPinSdD1 = 7;
constexpr int kPinSdD2 = 8;
constexpr int kPinSdD3 = 18;

// --- ES8311 + amp (I2C shared with AXP on 41/42) ---
constexpr int kPinI2cSda = 41;
constexpr int kPinI2cScl = 42;
constexpr int kPinI2sMclk = 13;
constexpr int kPinI2sBclk = 14;
constexpr int kPinI2sWs = 47;
constexpr int kPinI2sDin = 21;
constexpr int kPinI2sDout = 48;
constexpr int kPinPaCtrl = 39;  // amp enable, active-high

}  // namespace pocket::board
