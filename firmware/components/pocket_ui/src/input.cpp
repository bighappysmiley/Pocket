#include "pocket/input.hpp"

namespace pocket {

void InputMapper::push(InputEvent e) {
  int next = (q_tail_ + 1) % kQueue;
  if (next == q_head_) {
    // Queue full: never drop rotary detents (PIN wheel / focus). Fold into net.
    if (e == InputEvent::Up) {
      --rotary_overflow_;
      return;
    }
    if (e == InputEvent::Down) {
      ++rotary_overflow_;
      return;
    }
    return;
  }
  queue_[q_tail_] = e;
  q_tail_ = next;
}

InputEvent InputMapper::poll() {
  if (q_head_ == q_tail_) return InputEvent::None;
  InputEvent e = queue_[q_head_];
  q_head_ = (q_head_ + 1) % kQueue;
  return e;
}

InputEvent InputMapper::peek() const {
  if (q_head_ == q_tail_) return InputEvent::None;
  return queue_[q_head_];
}

int InputMapper::drain_up_down_net() {
  int net = rotary_overflow_;
  rotary_overflow_ = 0;
  while (q_head_ != q_tail_) {
    const InputEvent e = queue_[q_head_];
    if (e == InputEvent::Up) {
      --net;
      q_head_ = (q_head_ + 1) % kQueue;
    } else if (e == InputEvent::Down) {
      ++net;
      q_head_ = (q_head_ + 1) % kQueue;
    } else {
      break;
    }
  }
  return net;
}

void InputMapper::discard_nav_gestures() {
  // Compact queue in place — drop Power/Home/Back/PTT; keep rotary + Select.
  const int n = (q_tail_ - q_head_ + kQueue) % kQueue;
  InputEvent kept[kQueue];
  int k = 0;
  for (int i = 0; i < n; ++i) {
    const InputEvent e = queue_[(q_head_ + i) % kQueue];
    if (e == InputEvent::Power || e == InputEvent::Home || e == InputEvent::Back ||
        e == InputEvent::PttStart || e == InputEvent::PttStop) {
      continue;
    }
    kept[k++] = e;
  }
  q_head_ = 0;
  q_tail_ = 0;
  for (int i = 0; i < k; ++i) push(kept[i]);
  // SoftAP RF often leaves PWR latched low — require a clean release before next Power.
  pwr_edge_armed_ = false;
  boot_down_ = false;
  ptt_armed_ = false;
  fn_long_fired_ = fn_down_;  // don't fire Home from a hold that spanned SoftAP start
}

bool InputMapper::debounce(bool& seen, uint32_t& last_ms, uint32_t now_ms) {
  if (seen && (now_ms - last_ms) < InputThresholds::kDebounceMs) return false;
  seen = true;
  last_ms = now_ms;
  return true;
}

void InputMapper::on_button_up(bool pressed, uint32_t now_ms) {
  if (!pressed) return;
  if (!debounce(up_seen_, last_up_ms_, now_ms)) return;
  push(InputEvent::Up);
}

void InputMapper::on_button_down(bool pressed, uint32_t now_ms) {
  if (!pressed) return;
  if (!debounce(down_seen_, last_down_ms_, now_ms)) return;
  push(InputEvent::Down);
}

void InputMapper::on_button_function(bool pressed, uint32_t now_ms) {
  if (!debounce(fn_seen_, last_fn_ms_, now_ms)) return;
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
  if (!debounce(boot_seen_, last_boot_ms_, now_ms)) return;
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

void InputMapper::on_pwr(bool pressed, uint32_t now_ms) {
  if (!debounce(pwr_seen_, last_pwr_ms_, now_ms)) return;
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