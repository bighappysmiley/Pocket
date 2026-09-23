#pragma once
#include <cstdint>

namespace pocket {

enum class RefreshMode : uint8_t { Partial, Full };

/** Ghosting mitigation: after N=8 consecutive partials → force full (Spec §6). */
class RefreshPolicy {
 public:
  static constexpr int kGhostingN = 8;

  RefreshMode plan(bool wants_partial);
  void on_screen_enter_full();
  void on_applied(RefreshMode mode);
  int partial_count() const { return partial_count_; }

 private:
  int partial_count_ = 0;
};

}  // namespace pocket