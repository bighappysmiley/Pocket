#include "esp_ota_platform.hpp"

#include "esp_crt_bundle.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

namespace {

constexpr const char* TAG = "pocket_ota";

}  // namespace

bool EspOta::apply_https_ota(const std::string& url, std::string* status_out) {
  if (url.empty() || url.rfind("https://", 0) != 0) {
    if (status_out) *status_out = "Update URL missing.";
    return false;
  }

  esp_http_client_config_t http = {};
  http.url = url.c_str();
  http.timeout_ms = 120000;
  http.keep_alive_enable = true;
  http.crt_bundle_attach = esp_crt_bundle_attach;
  http.buffer_size = 4096;
  http.buffer_size_tx = 1024;
  http.max_redirection_count = 8;
  http.user_agent = "Pocket-OTA/1.0";

  esp_https_ota_config_t ota = {};
  ota.http_config = &http;

  if (status_out) *status_out = "Downloading update…";
  ESP_LOGI(TAG, "OTA begin %s", url.c_str());
  esp_err_t err = esp_https_ota(&ota);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_https_ota: %s", esp_err_to_name(err));
    if (status_out) *status_out = "Couldn't install update.";
    return false;
  }
  if (status_out) *status_out = "Update installed. Restarting…";
  ESP_LOGI(TAG, "OTA success — restarting");
  esp_restart();
  return true;  // unreachable
}
