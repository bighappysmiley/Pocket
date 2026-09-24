#pragma once
#include "pocket/app.hpp"
#include <string>

/** HTTPS A/B OTA into the inactive app slot (esp_https_ota). */
struct EspOta : pocket::PlatformOta {
  bool apply_https_ota(const std::string& url, std::string* status_out) override;
};
