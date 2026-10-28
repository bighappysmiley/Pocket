#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace pocket {

/**
 * Offline-capable wall clock: sync when online, then keep counting from a
 * monotonic / RTC timebase. Persist last anchor to NVS so deep sleep and
 * soft resets can resume; after full power-loss (RTC reset) restore last
 * known wall time as Approximate until the next network sync.
 *
 * Pure logic — host-testable; ESP wires mono/RTC/NVS around it.
 */
class OfflineWallClock {
 public:
  enum class Quality : uint8_t {
    None = 0,         // never synced — do not display a wall time
    Approximate = 1,  // restored after power-loss; counting but may be behind
    Trusted = 2,      // network sync or continuous RTC chain
  };

  static constexpr uint32_t kMagic = 0x43574B50u;  // 'PKWC' LE
  static constexpr uint16_t kVersion = 1;
  static constexpr size_t kBlobSize = 32;  // packed snapshot bytes

  void clear();

  /** Anchor from a trusted source (SNTP / hardware RTC). */
  void sync_trusted(int64_t unix_sec, uint64_t mono_ms, uint64_t rtc_us);

  /**
   * Restore a previously packed snapshot.
   * - rtc_continuous: board RTC kept running (deep sleep / soft reset) and
   *   rtc_now_us >= saved rtc → Trusted advance by RTC delta.
   * - otherwise: seed Approximate from last wall at mono_now (uptime advances).
   */
  bool restore(const uint8_t* blob, size_t len, uint64_t mono_now_ms, uint64_t rtc_now_us,
               bool rtc_continuous);

  /** Pack current anchor for NVS (call after sync / periodic capture). */
  bool pack(std::vector<uint8_t>& out) const;
  bool pack(uint8_t* out, size_t out_len) const;

  /** Refresh anchor to "now" so a persist reflects the latest wall + bases. */
  void capture(uint64_t mono_ms, uint64_t rtc_us);

  int64_t now_unix(uint64_t mono_ms) const;
  Quality quality() const { return quality_; }
  bool valid() const { return quality_ != Quality::None; }
  bool trusted() const { return quality_ == Quality::Trusted; }
  bool needs_network_sync() const { return quality_ != Quality::Trusted; }

  void local_hm(uint64_t mono_ms, int& hour, int& minute, int& weekday, int& month,
                int& day) const;

  int64_t anchor_unix() const { return anchor_unix_; }
  uint64_t anchor_mono_ms() const { return anchor_mono_ms_; }
  uint64_t anchor_rtc_us() const { return anchor_rtc_us_; }

 private:
  Quality quality_ = Quality::None;
  int64_t anchor_unix_ = 0;
  uint64_t anchor_mono_ms_ = 0;
  uint64_t anchor_rtc_us_ = 0;
};

}  // namespace pocket
