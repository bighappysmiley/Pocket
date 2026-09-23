#pragma once
/**
 * Waveshare ESP32-S3-ePaper-3.97 — Pocket pin map (Part C).
 * No volume keys. Document schematic nets for board bring-up.
 *
 * Rotary (schematic nets):
 *   Button_Up, Button_Down, Button_Function
 * Side:
 *   BOOT — short Back, hold ≥200 ms PTT
 *   PWR  — sleep / wake
 * Audio:
 *   ES8311 + onboard mic for PTT / mic test
 *
 * Replace GPIO numbers with the Waveshare schematic values for your board rev.
 */
namespace pocket::board {

constexpr int kPinButtonUp = 2;        // TODO: confirm vs Waveshare schematic
constexpr int kPinButtonDown = 3;
constexpr int kPinButtonFunction = 4;
constexpr int kPinBoot = 0;            // ESP32-S3 BOOT
constexpr int kPinPwr = 1;             // case PWR pad

constexpr int kLogicalW = 480;
constexpr int kLogicalH = 800;
// Native panel 800×480 landscape; firmware applies 90° rotation to logical portrait.

}  // namespace pocket::board