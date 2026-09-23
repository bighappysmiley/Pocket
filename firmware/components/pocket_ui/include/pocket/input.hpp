#pragma once
#include <cstdint>

namespace pocket {

/** Part C hardware-controls-v1.2 — authoritative input map. No volume keys. */
enum class InputEvent : uint8_t {
  None = 0,
  Up,        // Button_Up
  Down,      // Button_Down
  Select,    // Button_Function short < 800 ms
  Home,      // Button_Function long >= 800 ms
  Back,      // BOOT short (< PTT arm)
  PttStart,  // BOOT hold >= 200 ms
  PttStop,   // BOOT release after PTT armed
  Power,     // PWR short
};

struct InputThresholds {
  static constexpr uint32_t kFunctionLongMs = 800;
  static constexpr uint32_t kPttArmMs = 200;
};

/**
 * Debounce / classify raw button edges into Spec gestures.
 * Call tick() while any button may be held so long-Home / PTT can arm.
 */
class InputMapper {
 public:
  void on_button_up(bool pressed, uint32_t now_ms);
  void on_button_down(bool pressed, uint32_t now_ms);
  void on_button_function(bool pressed, uint32_t now_ms);
  void on_boot(bool pressed, uint32_t now_ms);
  void on_pwr(bool pressed, uint32_t now_ms);
  void tick(uint32_t now_ms);

  InputEvent poll();

 private:
  void push(InputEvent e);

  static constexpr int kQueue = 8;
  InputEvent queue_[kQueue]{};
  int q_head_ = 0;
  int q_tail_ = 0;

  bool fn_down_ = false;
  uint32_t fn_down_ms_ = 0;
  bool fn_long_fired_ = false;

  bool boot_down_ = false;
  uint32_t boot_down_ms_ = 0;
  bool ptt_armed_ = false;

  bool pwr_edge_armed_ = true;
};

}  // namespace pocket