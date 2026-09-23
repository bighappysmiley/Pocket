#include "pocket/input.hpp"

namespace pocket {

void InputMapper::push(InputEvent e) {
  int next = (q_tail_ + 1) % kQueue;
  if (next == q_head_) return;
  queue_[q_tail_] = e;
  q_tail_ = next;
}

InputEvent InputMapper::poll() {
  if (q_head_ == q_tail_) return InputEvent::None;
  InputEvent e = queue_[q_head_];
  q_head_ = (q_head_ + 1) % kQueue;
  return e;
}

void InputMapper::on_button_up(bool pressed, uint32_t /*now_ms*/) {
  if (pressed) push(InputEvent::Up);
}

void InputMapper::on_button_down(bool pressed, uint32_t /*now_ms*/) {
  if (pressed) push(InputEvent::Down);
}

void InputMapper::on_button_function(bool pressed, uint32_t now_ms) {
  if (pressed) {
    if (!fn_down_) {
      fn_down_ = true;
      fn_down_ms_ = now_ms;
      fn_long_fired_ = false;
    }
    return;
  }
  if (!fn_down_) return;
  fn_down_ = false;
  if (fn_long_fired_) return;
  push(InputEvent::Select);
}

void InputMapper::on_boot(bool pressed, uint32_t now_ms) {
  if (pressed) {
    if (!boot_down_) {
      boot_down_ = true;
      boot_down_ms_ = now_ms;
      ptt_armed_ = false;
    }
    return;
  }
  if (!boot_down_) return;
  boot_down_ = false;
  if (ptt_armed_) {
    push(InputEvent::PttStop);
    ptt_armed_ = false;
    return;  // Spec: release after PTT arm does not fire Back
  }
  push(InputEvent::Back);
}

void InputMapper::on_pwr(bool pressed, uint32_t /*now_ms*/) {
  if (pressed) {
    if (pwr_edge_armed_) {
      push(InputEvent::Power);
      pwr_edge_armed_ = false;
    }
  } else {
    pwr_edge_armed_ = true;
  }
}

void InputMapper::tick(uint32_t now_ms) {
  if (fn_down_ && !fn_long_fired_) {
    if (now_ms - fn_down_ms_ >= InputThresholds::kFunctionLongMs) {
      push(InputEvent::Home);
      fn_long_fired_ = true;
    }
  }
  if (boot_down_ && !ptt_armed_) {
    if (now_ms - boot_down_ms_ >= InputThresholds::kPttArmMs) {
      push(InputEvent::PttStart);
      ptt_armed_ = true;
    }
  }
}

}  // namespace pocket