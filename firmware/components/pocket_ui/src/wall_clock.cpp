#include "pocket/wall_clock.hpp"

#include <cstddef>
#include <cstring>
#include <ctime>

namespace pocket {
namespace {

void put_u16(uint8_t*& p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xff);
  p[1] = static_cast<uint8_t>((v >> 8) & 0xff);
  p += 2;
}
void put_u32(uint8_t*& p, uint32_t v) {
  for (int i = 0; i < 4; ++i) {
    p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
  }
  p += 4;
}
void put_u64(uint8_t*& p, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    p[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xff);
  }
  p += 8;
}
void put_i64(uint8_t*& p, int64_t v) { put_u64(p, static_cast<uint64_t>(v)); }

bool get_u16(const uint8_t*& p, const uint8_t* end, uint16_t& v) {
  if (end - p < 2) return false;
  v = static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
  p += 2;
  return true;
}
bool get_u32(const uint8_t*& p, const uint8_t* end, uint32_t& v) {
  if (end - p < 4) return false;
  v = 0;
  for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(p[i]) << (8 * i);
  p += 4;
  return true;
}
bool get_u64(const uint8_t*& p, const uint8_t* end, uint64_t& v) {
  if (end - p < 8) return false;
  v = 0;
  for (int i = 0; i < 8; ++i) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  p += 8;
  return true;
}
bool get_i64(const uint8_t*& p, const uint8_t* end, int64_t& v) {
  uint64_t u = 0;
  if (!get_u64(p, end, u)) return false;
  v = static_cast<int64_t>(u);
  return true;
}

/** Reject clearly bogus unix (pre-2024 / far future). */
bool unix_plausible(int64_t u) {
  constexpr int64_t kMin = 1704067200LL;   // 2024-01-01 UTC
  constexpr int64_t kMax = 4102444800LL;   // 2100-01-01 UTC
  return u >= kMin && u < kMax;
}

}  // namespace

void OfflineWallClock::clear() {
  quality_ = Quality::None;
  anchor_unix_ = 0;
  anchor_mono_ms_ = 0;
  anchor_rtc_us_ = 0;
}

void OfflineWallClock::sync_trusted(int64_t unix_sec, uint64_t mono_ms, uint64_t rtc_us) {
  if (!unix_plausible(unix_sec)) return;
  anchor_unix_ = unix_sec;
  anchor_mono_ms_ = mono_ms;
  anchor_rtc_us_ = rtc_us;
  quality_ = Quality::Trusted;
}

bool OfflineWallClock::restore(const uint8_t* blob, size_t len, uint64_t mono_now_ms,
                               uint64_t rtc_now_us, bool rtc_continuous) {
  clear();
  if (!blob || len < kBlobSize) return false;
  const uint8_t* p = blob;
  const uint8_t* end = blob + len;
  uint32_t magic = 0;
  uint16_t ver = 0;
  uint16_t flags = 0;
  int64_t wall = 0;
  uint64_t mono = 0;
  uint64_t rtc = 0;
  if (!get_u32(p, end, magic) || magic != kMagic) return false;
  if (!get_u16(p, end, ver) || ver != kVersion) return false;
  if (!get_u16(p, end, flags)) return false;
  if (!get_i64(p, end, wall) || !get_u64(p, end, mono) || !get_u64(p, end, rtc)) return false;
  if ((flags & 0x1u) == 0) return false;
  if (!unix_plausible(wall)) return false;

  if (rtc_continuous && rtc_now_us >= rtc && rtc != 0) {
    const uint64_t d_us = rtc_now_us - rtc;
    const int64_t advanced = wall + static_cast<int64_t>(d_us / 1000000ull);
    if (!unix_plausible(advanced)) return false;
    anchor_unix_ = advanced;
    anchor_mono_ms_ = mono_now_ms;
    anchor_rtc_us_ = rtc_now_us;
    quality_ = Quality::Trusted;
    return true;
  }

  // Power-loss / RTC reset: last known wall, then count with monotonic uptime.
  anchor_unix_ = wall;
  anchor_mono_ms_ = mono_now_ms;
  anchor_rtc_us_ = rtc_now_us;
  quality_ = Quality::Approximate;
  return true;
}

bool OfflineWallClock::pack(uint8_t* out, size_t out_len) const {
  if (!out || out_len < kBlobSize) return false;
  if (quality_ == Quality::None) return false;
  uint8_t* p = out;
  put_u32(p, kMagic);
  put_u16(p, kVersion);
  put_u16(p, 0x1u);  // ever_synced
  put_i64(p, anchor_unix_);
  put_u64(p, anchor_mono_ms_);
  put_u64(p, anchor_rtc_us_);
  return (p - out) == static_cast<ptrdiff_t>(kBlobSize);
}

bool OfflineWallClock::pack(std::vector<uint8_t>& out) const {
  out.assign(kBlobSize, 0);
  if (!pack(out.data(), out.size())) {
    out.clear();
    return false;
  }
  return true;
}

void OfflineWallClock::capture(uint64_t mono_ms, uint64_t rtc_us) {
  if (quality_ == Quality::None) return;
  anchor_unix_ = now_unix(mono_ms);
  anchor_mono_ms_ = mono_ms;
  anchor_rtc_us_ = rtc_us;
}

int64_t OfflineWallClock::now_unix(uint64_t mono_ms) const {
  if (quality_ == Quality::None) return 0;
  if (mono_ms >= anchor_mono_ms_) {
    return anchor_unix_ + static_cast<int64_t>((mono_ms - anchor_mono_ms_) / 1000ull);
  }
  // Monotonic went backwards (unexpected wrap) — hold anchor.
  return anchor_unix_;
}

void OfflineWallClock::local_hm(uint64_t mono_ms, int& hour, int& minute, int& weekday, int& month,
                               int& day) const {
  hour = 0;
  minute = 0;
  weekday = 0;
  month = 0;
  day = 1;
  if (quality_ == Quality::None) return;
  const time_t t = static_cast<time_t>(now_unix(mono_ms));
  struct tm tm {};
  // gmtime: callers apply TZ via setenv/tzset + localtime on device; host tests
  // use UTC so results are deterministic. Device EspClock uses localtime_r on
  // system time after settimeofday — this path is for the pure logic object.
  gmtime_r(&t, &tm);
  hour = tm.tm_hour;
  minute = tm.tm_min;
  weekday = tm.tm_wday;
  month = tm.tm_mon;
  day = tm.tm_mday;
}

}  // namespace pocket
