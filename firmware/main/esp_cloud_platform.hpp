#pragma once
#include "pocket/app.hpp"

/** Device → Pocket Cloud HTTP (pair sessions + status poll + music sync). */
struct EspCloud : pocket::PlatformCloud {
  std::string create_pair_session(const std::string& device_id) override;
  std::string pair_status(const std::string& code) override;
  void refresh_entitlement(pocket::DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
  std::string music_list_json(const std::string& device_id, bool sd_present = false) override;
  std::vector<uint8_t> music_download(const std::string& device_id, const std::string& track_id) override;
  std::string books_list_json(const std::string& device_id) override;
  std::vector<uint8_t> book_download(const std::string& device_id, const std::string& book_id) override;
  std::string firmware_latest_json() override;
  std::string device_attest_json(const std::string& device_id) override;
  bool push_device_settings(const std::string& device_id, int volume_percent, int brightness_percent) override;
};
