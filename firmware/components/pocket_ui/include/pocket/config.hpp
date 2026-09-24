#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace pocket {

/** Device config (Spec §11.1 + Part B). Persisted via NVS and optional SD mirror. */
struct DeviceConfig {
  std::string device_name = "Pocket";
  uint8_t pin_length = 4;
  std::array<uint8_t, 32> pin_hash{};
  uint8_t pin_salt[16]{};
  bool onboarding_complete = false;
  std::string tz_id = "America/New_York";
  uint8_t time_format = 12;  // 12 or 24
  std::string wifi_ssid;
  uint8_t stt_path = 0;  // 0=cloud, 1=ondevice
  uint8_t weather_units = 0;  // 0=F, 1=C
  std::string weather_city;
  float weather_lat = 0;
  float weather_lon = 0;
  uint16_t home_visible = 0x00FF;  // bits 0..7 Notes..Update; Settings+Update always on
  uint16_t idle_lock_s = 60;
  bool show_batt_pct = true;
  bool cloud_entitled = false;
  bool companion_linked = false;
  std::string fw_channel = "stable";
  std::string fw_version = "0.1.0";
  std::string fw_build_id;  // e.g. POCKET-LIVE-v33-… for OTA compare
  std::string device_id;  // UUID
  std::string device_token;
  std::string cloud_status = "free";  // free|trialing|active|lapsed
};

enum class HomeApp : uint8_t {
  Notes = 0,
  Ledger = 1,
  Clock = 2,
  Pass = 3,
  Weather = 4,
  Music = 5,
  Settings = 6,
  Update = 7,
  /** Reserved Home grid slots 8..15 (empty until assigned). */
  Slot8 = 8,
  Slot9 = 9,
  Slot10 = 10,
  Slot11 = 11,
  Slot12 = 12,
  Slot13 = 13,
  Slot14 = 14,
  Slot15 = 15,
};

inline constexpr int kHomeGridSlots = 16;

inline bool home_app_is_real(HomeApp a) {
  return static_cast<uint8_t>(a) <= static_cast<uint8_t>(HomeApp::Update);
}

inline bool home_app_visible(const DeviceConfig& c, HomeApp a) {
  if (a == HomeApp::Settings || a == HomeApp::Update) return true;
  if (!home_app_is_real(a)) return false;
  return (c.home_visible & (1u << static_cast<uint8_t>(a))) != 0;
}

class ConfigStore {
 public:
  virtual ~ConfigStore() = default;
  virtual DeviceConfig load() = 0;
  virtual void save(const DeviceConfig& cfg) = 0;
};

/** In-memory store for host sim / tests. */
class MemoryConfigStore : public ConfigStore {
 public:
  DeviceConfig load() override { return cfg_; }
  void save(const DeviceConfig& cfg) override { cfg_ = cfg; }
  DeviceConfig& mut() { return cfg_; }

 private:
  DeviceConfig cfg_{};
};

/** Host/tests: persist a packed DeviceConfig blob to a filesystem path. */
class FileConfigStore : public ConfigStore {
 public:
  explicit FileConfigStore(std::string path) : path_(std::move(path)) {}
  DeviceConfig load() override;
  void save(const DeviceConfig& cfg) override;

 private:
  std::string path_;
};

/** Pack/unpack versioned DeviceConfig blobs (NVS + SD / file). */
bool pack_device_config(const DeviceConfig& cfg, std::vector<uint8_t>& out);
bool unpack_device_config(const uint8_t* data, size_t len, DeviceConfig& out);

bool verify_pin(const DeviceConfig& cfg, std::string_view digits);
void set_pin(DeviceConfig& cfg, std::string_view digits);

}  // namespace pocket
