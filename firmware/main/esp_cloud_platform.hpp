#pragma once
#include "pocket/app.hpp"

/** Device → Pocket Cloud HTTP (pair sessions + status poll). */
struct EspCloud : pocket::PlatformCloud {
  std::string create_pair_session(const std::string& device_id) override;
  std::string pair_status(const std::string& code) override;
  void refresh_entitlement(pocket::DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
};
