#pragma once
#include <cstdint>

namespace pocket {

enum class RefreshMode : uint8_t { Partial, Full };

/**
 * Ghosting mitigation: after N consecutive partials → force full (Spec §6).
 * Tiny status/PIN region updates count less so the bar/clock don't force a full flash.
 */
class RefreshPolicy {
 public:
  static constexpr int kGhostingN = 16;

  RefreshMode plan(bool wants_partial);
  void on_screen_enter_full();
  void on_applied(RefreshMode mode);
  /** Status bar / digit region — counts as half a partial toward ghosting. */
  void on_applied_tiny(RefreshMode mode);
  int partial_count() const { return partial_count_; }

 private:
  int partial_count_ = 0;
  int tiny_credit_ = 0;
};

}  // namespace pocket