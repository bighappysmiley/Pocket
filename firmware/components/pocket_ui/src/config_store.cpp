#include "pocket/config.hpp"
#include <cstring>
#include <functional>

namespace pocket {

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