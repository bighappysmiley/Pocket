#include "pocket/config.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>

namespace pocket {
namespace {

constexpr uint32_t kMagic = 0x314B4350u;  // 'PCK1' LE
constexpr uint16_t kVersion = 1;
constexpr size_t kMaxString = 128;
constexpr size_t kMaxBlob = 4096;

void put_u8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }

void put_u16(std::vector<uint8_t>& o, uint16_t v) {
  o.push_back(static_cast<uint8_t>(v & 0xff));
  o.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
}

void put_u32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(static_cast<uint8_t>(v & 0xff));
  o.push_back(static_cast<uint8_t>((v >> 8) & 0xff));
  o.push_back(static_cast<uint8_t>((v >> 16) & 0xff));
  o.push_back(static_cast<uint8_t>((v >> 24) & 0xff));
}

void put_f32(std::vector<uint8_t>& o, float v) {
  uint32_t bits = 0;
  static_assert(sizeof(float) == 4, "float must be 32-bit");
  std::memcpy(&bits, &v, 4);
  put_u32(o, bits);
}

void put_bytes(std::vector<uint8_t>& o, const uint8_t* p, size_t n) {
  o.insert(o.end(), p, p + n);
}

void put_str(std::vector<uint8_t>& o, const std::string& s) {
  const size_t n = s.size() > kMaxString ? kMaxString : s.size();
  put_u16(o, static_cast<uint16_t>(n));
  if (n) put_bytes(o, reinterpret_cast<const uint8_t*>(s.data()), n);
}

bool need(size_t& off, size_t len, size_t n) { return off + n <= len; }

bool get_u8(const uint8_t* d, size_t len, size_t& off, uint8_t& v) {
  if (!need(off, len, 1)) return false;
  v = d[off++];
  return true;
}

bool get_u16(const uint8_t* d, size_t len, size_t& off, uint16_t& v) {
  if (!need(off, len, 2)) return false;
  v = static_cast<uint16_t>(d[off] | (d[off + 1] << 8));
  off += 2;
  return true;
}

bool get_u32(const uint8_t* d, size_t len, size_t& off, uint32_t& v) {
  if (!need(off, len, 4)) return false;
  v = static_cast<uint32_t>(d[off] | (d[off + 1] << 8) | (d[off + 2] << 16) | (d[off + 3] << 24));
  off += 4;
  return true;
}

bool get_f32(const uint8_t* d, size_t len, size_t& off, float& v) {
  uint32_t bits = 0;
  if (!get_u32(d, len, off, bits)) return false;
  std::memcpy(&v, &bits, 4);
  return true;
}

bool get_bytes(const uint8_t* d, size_t len, size_t& off, uint8_t* dest, size_t n) {
  if (!need(off, len, n)) return false;
  std::memcpy(dest, d + off, n);
  off += n;
  return true;
}

bool get_str(const uint8_t* d, size_t len, size_t& off, std::string& out) {
  uint16_t n = 0;
  if (!get_u16(d, len, off, n)) return false;
  if (n > kMaxString) return false;
  if (!need(off, len, n)) return false;
  out.assign(reinterpret_cast<const char*>(d + off), n);
  off += n;
  return true;
}

}  // namespace

bool pack_device_config(const DeviceConfig& cfg, std::vector<uint8_t>& out) {
  out.clear();
  out.reserve(512);
  put_u32(out, kMagic);
  put_u16(out, kVersion);
  put_u16(out, 0);  // flags
  put_u8(out, cfg.pin_length);
  put_bytes(out, cfg.pin_hash.data(), cfg.pin_hash.size());
  put_bytes(out, cfg.pin_salt, sizeof(cfg.pin_salt));
  put_u8(out, cfg.onboarding_complete ? 1 : 0);
  put_u8(out, cfg.time_format);
  put_u8(out, cfg.stt_path);
  put_u8(out, cfg.weather_units);
  put_u8(out, cfg.home_visible);
  put_u8(out, cfg.show_batt_pct ? 1 : 0);
  put_u8(out, cfg.cloud_entitled ? 1 : 0);
  put_u8(out, cfg.companion_linked ? 1 : 0);
  put_u16(out, cfg.idle_lock_s);
  put_f32(out, cfg.weather_lat);
  put_f32(out, cfg.weather_lon);
  put_str(out, cfg.device_name);
  put_str(out, cfg.tz_id);
  put_str(out, cfg.wifi_ssid);
  put_str(out, cfg.weather_city);
  put_str(out, cfg.fw_channel);
  put_str(out, cfg.fw_version);
  put_str(out, cfg.device_id);
  put_str(out, cfg.device_token);
  put_str(out, cfg.cloud_status);
  return out.size() <= kMaxBlob;
}

bool unpack_device_config(const uint8_t* data, size_t len, DeviceConfig& out) {
  if (!data || len < 8 || len > kMaxBlob) return false;
  size_t off = 0;
  uint32_t magic = 0;
  uint16_t ver = 0;
  uint16_t flags = 0;
  if (!get_u32(data, len, off, magic) || magic != kMagic) return false;
  if (!get_u16(data, len, off, ver) || ver != kVersion) return false;
  if (!get_u16(data, len, off, flags)) return false;
  (void)flags;

  DeviceConfig cfg;
  uint8_t b = 0;
  if (!get_u8(data, len, off, cfg.pin_length)) return false;
  if (!get_bytes(data, len, off, cfg.pin_hash.data(), cfg.pin_hash.size())) return false;
  if (!get_bytes(data, len, off, cfg.pin_salt, sizeof(cfg.pin_salt))) return false;
  if (!get_u8(data, len, off, b)) return false;
  cfg.onboarding_complete = b != 0;
  if (!get_u8(data, len, off, cfg.time_format)) return false;
  if (!get_u8(data, len, off, cfg.stt_path)) return false;
  if (!get_u8(data, len, off, cfg.weather_units)) return false;
  if (!get_u8(data, len, off, cfg.home_visible)) return false;
  if (!get_u8(data, len, off, b)) return false;
  cfg.show_batt_pct = b != 0;
  if (!get_u8(data, len, off, b)) return false;
  cfg.cloud_entitled = b != 0;
  if (!get_u8(data, len, off, b)) return false;
  cfg.companion_linked = b != 0;
  if (!get_u16(data, len, off, cfg.idle_lock_s)) return false;
  if (!get_f32(data, len, off, cfg.weather_lat)) return false;
  if (!get_f32(data, len, off, cfg.weather_lon)) return false;
  if (!get_str(data, len, off, cfg.device_name)) return false;
  if (!get_str(data, len, off, cfg.tz_id)) return false;
  if (!get_str(data, len, off, cfg.wifi_ssid)) return false;
  if (!get_str(data, len, off, cfg.weather_city)) return false;
  if (!get_str(data, len, off, cfg.fw_channel)) return false;
  if (!get_str(data, len, off, cfg.fw_version)) return false;
  if (!get_str(data, len, off, cfg.device_id)) return false;
  if (!get_str(data, len, off, cfg.device_token)) return false;
  if (!get_str(data, len, off, cfg.cloud_status)) return false;

  out = std::move(cfg);
  return true;
}

DeviceConfig FileConfigStore::load() {
  DeviceConfig cfg;
  std::ifstream in(path_, std::ios::binary);
  if (!in) return cfg;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (buf.empty()) return cfg;
  DeviceConfig parsed;
  if (unpack_device_config(buf.data(), buf.size(), parsed)) return parsed;
  return cfg;
}

void FileConfigStore::save(const DeviceConfig& cfg) {
  std::vector<uint8_t> blob;
  if (!pack_device_config(cfg, blob)) return;
  std::ofstream out(path_, std::ios::binary | std::ios::trunc);
  if (!out) return;
  out.write(reinterpret_cast<const char*>(blob.data()), static_cast<std::streamsize>(blob.size()));
}

// Lightweight salted hash for PIN (not a substitute for hardware secure element).
static void hash_pin(const uint8_t salt[16], std::string_view digits, std::array<uint8_t, 32>& out) {
  // FNV-1a style mix expanded to 32 bytes — host/tests; device builds should use mbedtls SHA-256.
  uint64_t h = 14695981039346656037ull;
  for (int i = 0; i < 16; ++i) {
    h ^= salt[i];
    h *= 1099511628211ull;
  }
  for (char c : digits) {
    h ^= static_cast<uint8_t>(c);
    h *= 1099511628211ull;
  }
  for (int i = 0; i < 32; ++i) {
    h ^= static_cast<uint8_t>(i * 17 + digits.size());
    h *= 1099511628211ull;
    out[i] = static_cast<uint8_t>(h >> ((i % 8) * 8));
  }
}

bool verify_pin(const DeviceConfig& cfg, std::string_view digits) {
  if (digits.size() != cfg.pin_length) return false;
  std::array<uint8_t, 32> got{};
  hash_pin(cfg.pin_salt, digits, got);
  return got == cfg.pin_hash;
}

void set_pin(DeviceConfig& cfg, std::string_view digits) {
  cfg.pin_length = static_cast<uint8_t>(digits.size());
  // Derive salt from digits length + fixed seed for sim; ESP build should use RNG.
  for (int i = 0; i < 16; ++i) cfg.pin_salt[i] = static_cast<uint8_t>(0xA5 ^ i ^ digits.size());
  hash_pin(cfg.pin_salt, digits, cfg.pin_hash);
}

}  // namespace pocket
