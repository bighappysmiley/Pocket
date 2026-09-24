#pragma once
#include "pocket/app.hpp"

/** ESP-IDF Station + SoftAP provisioning for Pocket onboarding. */
struct EspWifi : pocket::PlatformWifi {
  std::vector<std::string> scan() override;
  bool connect(const std::string& ssid, const std::string& pass) override;
  bool connected() const override;
  bool start_provision(const std::string& preferred_ssid, std::string* ap_ssid_out,
                       std::string* ap_pass_out) override;
  void stop_provision() override;
  bool take_provision_credentials(std::string* ssid, std::string* password) override;
  std::string provision_ap_ssid() const override;
  std::string provision_ap_password() const override;
};
