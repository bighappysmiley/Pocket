#include "pocket/wall_clock.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

using namespace pocket;

static int failures = 0;
#define CHECK(cond)                                                         \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

int main() {
  OfflineWallClock clk;

  // Fresh — unsynced.
  CHECK(clk.quality() == OfflineWallClock::Quality::None);
  CHECK(!clk.valid());
  CHECK(clk.needs_network_sync());
  CHECK(clk.now_unix(5000) == 0);

  // Sync when online.
  constexpr int64_t kT0 = 1730000000LL;  // ~2024-10-27 UTC
  clk.sync_trusted(kT0, /*mono*/ 1000, /*rtc*/ 5'000'000ull);
  CHECK(clk.trusted());
  CHECK(clk.valid());
  CHECK(!clk.needs_network_sync());
  CHECK(clk.now_unix(1000) == kT0);
  // Advance 90 seconds offline via monotonic.
  CHECK(clk.now_unix(1000 + 90'000) == kT0 + 90);

  int h = -1, m = -1, wd = -1, mo = -1, d = -1;
  clk.local_hm(1000, h, m, wd, mo, d);
  CHECK(h >= 0 && h <= 23);
  CHECK(m >= 0 && m <= 59);

  // Persist + restore with RTC continuity (deep sleep / soft reset).
  std::vector<uint8_t> blob;
  CHECK(clk.pack(blob));
  CHECK(blob.size() == OfflineWallClock::kBlobSize);

  OfflineWallClock after_sleep;
  // 2 minutes of RTC advance, mono restarted at 50ms (new uptime).
  const uint64_t rtc_now = 5'000'000ull + 120ull * 1'000'000ull;
  CHECK(after_sleep.restore(blob.data(), blob.size(), /*mono*/ 50, rtc_now,
                            /*rtc_continuous*/ true));
  CHECK(after_sleep.trusted());
  CHECK(after_sleep.now_unix(50) == kT0 + 120);
  // Keep counting after restore.
  CHECK(after_sleep.now_unix(50 + 30'000) == kT0 + 150);

  // Persist + restore after power-loss (RTC reset / not continuous).
  OfflineWallClock after_power;
  CHECK(after_power.restore(blob.data(), blob.size(), /*mono*/ 10, /*rtc*/ 1000,
                            /*rtc_continuous*/ false));
  CHECK(after_power.valid());
  CHECK(!after_power.trusted());
  CHECK(after_power.quality() == OfflineWallClock::Quality::Approximate);
  CHECK(after_power.needs_network_sync());
  // Starts at last known wall, then advances with uptime.
  CHECK(after_power.now_unix(10) == kT0);
  CHECK(after_power.now_unix(10 + 60'000) == kT0 + 60);

  // Capture re-anchors for a fresh persist while Approximate.
  after_power.capture(10 + 60'000, 2000);
  std::vector<uint8_t> blob2;
  CHECK(after_power.pack(blob2));
  OfflineWallClock round2;
  CHECK(round2.restore(blob2.data(), blob2.size(), 5, 3000, false));
  CHECK(round2.now_unix(5) == kT0 + 60);

  // Corrupt / empty blob rejected.
  OfflineWallClock bad;
  CHECK(!bad.restore(nullptr, 0, 0, 0, false));
  uint8_t junk[OfflineWallClock::kBlobSize]{};
  CHECK(!bad.restore(junk, sizeof(junk), 0, 0, false));
  CHECK(bad.quality() == OfflineWallClock::Quality::None);

  // Reject pre-2024 sync.
  OfflineWallClock old;
  old.sync_trusted(1'000'000'000LL, 0, 0);  // ~2001
  CHECK(old.quality() == OfflineWallClock::Quality::None);

  // Pack while unsynced fails.
  OfflineWallClock empty;
  std::vector<uint8_t> noblob;
  CHECK(!empty.pack(noblob));
  CHECK(noblob.empty());

  if (failures) {
    std::printf("test_offline_clock FAILED (%d)\n", failures);
    return 1;
  }
  std::puts("test_offline_clock OK");
  return 0;
}
