#pragma once
#include "pocket/app.hpp"

/** ESP-IDF Station Wi-Fi (scan + connect) for Pocket onboarding. */
struct EspWifi : pocket::PlatformWifi {
  std::vector<std::string> scan() override;
  bool connect(const std::string& ssid, const std::string& pass) override;
  bool connected() const override;
};
