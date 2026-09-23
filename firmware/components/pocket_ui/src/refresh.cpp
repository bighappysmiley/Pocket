#include "pocket/refresh.hpp"

namespace pocket {

RefreshMode RefreshPolicy::plan(bool wants_partial) {
  if (!wants_partial) return RefreshMode::Full;
  if (partial_count_ >= kGhostingN) return RefreshMode::Full;
  return RefreshMode::Partial;
}

void RefreshPolicy::on_screen_enter_full() { partial_count_ = 0; }

void RefreshPolicy::on_applied(RefreshMode mode) {
  if (mode == RefreshMode::Full) {
    partial_count_ = 0;
  } else {
    ++partial_count_;
  }
}

}  // namespace pocket