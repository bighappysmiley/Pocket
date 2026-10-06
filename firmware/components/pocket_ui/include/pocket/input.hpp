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
  Back,      // Side button short (< PTT arm)
  PttStart,  // Side button hold >= 200 ms
  PttStop,   // Side button release after PTT armed
  Power,     // Power button short
};

struct InputThresholds {
  static constexpr uint32_t kFunctionLongMs = 800;
  static constexpr uint32_t kPttArmMs = 200;
  /**
   * Minimum time between accepted raw edges per control. The Waveshare 3-way
   * rocker and side buttons are read with zero hardware debounce (see
   * ButtonPoller::poll); mechanical bounce on a single physical press can
   * otherwise surface as a burst of rapid press/release edges, each one
   * turning into its own InputEvent — e.g. several extra Up/Down or Select
   * events — which in turn each drive a full present_canvas()/EPD refresh.
   * Debouncing here (platform-independent, host-testable) collapses a bounce
   * burst from one physical press into exactly one logical edge.
   */
  static constexpr uint32_t kDebounceMs = 60;
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
  /** Peek next queued event without consuming it (None if empty). */
  InputEvent peek() const;
  /**
   * Drain consecutive Up/Down events from the queue. Returns net steps
   * (Down = +1, Up = −1). Stops at the first non-rotary event.
   * Used so PIN digit spins present once with the final value.
   */
  int drain_up_down_net();

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

  /** Debounce bookkeeping — one last-accepted-edge timestamp per control. */
  bool up_seen_ = false;
  uint32_t last_up_ms_ = 0;
  bool down_seen_ = false;
  uint32_t last_down_ms_ = 0;
  bool fn_seen_ = false;
  uint32_t last_fn_ms_ = 0;
  bool boot_seen_ = false;
  uint32_t last_boot_ms_ = 0;
  bool pwr_seen_ = false;
  uint32_t last_pwr_ms_ = 0;

  /** True and records the edge if this control hasn't fired within the debounce window. */
  static bool debounce(bool& seen, uint32_t& last_ms, uint32_t now_ms);
};

}  // namespace pocket