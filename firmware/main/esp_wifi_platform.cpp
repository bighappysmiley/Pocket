#include "esp_wifi_platform.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

namespace {

constexpr const char* TAG = "pocket_wifi";
constexpr int kMaxAps = 24;
constexpr EventBits_t kBitConnected = BIT0;
constexpr EventBits_t kBitFail = BIT1;

EventGroupHandle_t s_wifi_events = nullptr;
bool s_wifi_ready = false;
bool s_sta_connected = false;

void wifi_event_handler(void* /*arg*/, esp_event_base_t base, int32_t id, void* /*data*/) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    s_sta_connected = false;
    if (s_wifi_events) xEventGroupSetBits(s_wifi_events, kBitFail);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    s_sta_connected = true;
    if (s_wifi_events) xEventGroupSetBits(s_wifi_events, kBitConnected);
  }
}

bool wifi_ensure() {
  if (s_wifi_ready) return true;

  ESP_ERROR_CHECK(esp_netif_init());
  esp_err_t err = esp_event_loop_create_default();
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    ESP_LOGE(TAG, "event loop: %s", esp_err_to_name(err));
    return false;
  }
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr,
                                                      nullptr));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr,
                                                      nullptr));
  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_start());

  s_wifi_events = xEventGroupCreate();
  s_wifi_ready = true;
  ESP_LOGI(TAG, "STA ready");
  return true;
}

}  // namespace

std::vector<std::string> EspWifi::scan() {
  std::vector<std::string> out;
  if (!wifi_ensure()) return out;

  wifi_scan_config_t sc = {};
  sc.show_hidden = false;
  sc.scan_type = WIFI_SCAN_TYPE_ACTIVE;
  sc.scan_time.active.min = 100;
  sc.scan_time.active.max = 300;

  esp_err_t err = esp_wifi_scan_start(&sc, true);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "scan_start: %s", esp_err_to_name(err));
    return out;
  }

  uint16_t ap_count = 0;
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_scan_get_ap_num(&ap_count));
  if (ap_count == 0) return out;
  if (ap_count > kMaxAps) ap_count = kMaxAps;

  std::vector<wifi_ap_record_t> records(ap_count);
  uint16_t n = ap_count;
  err = esp_wifi_scan_get_ap_records(&n, records.data());
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "get_ap_records: %s", esp_err_to_name(err));
    return out;
  }

  std::sort(records.begin(), records.begin() + n,
            [](const wifi_ap_record_t& a, const wifi_ap_record_t& b) { return a.rssi > b.rssi; });

  for (uint16_t i = 0; i < n; ++i) {
    const char* ssid = reinterpret_cast<const char*>(records[i].ssid);
    if (!ssid || !ssid[0]) continue;
    std::string name(ssid);
    if (std::find(out.begin(), out.end(), name) != out.end()) continue;
    out.push_back(std::move(name));
    if (out.size() >= 6) break;
  }
  ESP_LOGI(TAG, "scan found %u unique SSIDs", static_cast<unsigned>(out.size()));
  return out;
}

bool EspWifi::connect(const std::string& ssid, const std::string& pass) {
  if (!wifi_ensure()) return false;
  if (ssid.empty() || ssid.size() > 31) return false;
  if (pass.size() > 63) return false;

  wifi_config_t cfg = {};
  std::memcpy(cfg.sta.ssid, ssid.c_str(), ssid.size());
  std::memcpy(cfg.sta.password, pass.c_str(), pass.size());
  cfg.sta.threshold.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;

  if (s_wifi_events) xEventGroupClearBits(s_wifi_events, kBitConnected | kBitFail);
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
    return false;
  }

  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_events, kBitConnected | kBitFail, pdTRUE, pdFALSE, pdMS_TO_TICKS(20000));
  if (bits & kBitConnected) {
    ESP_LOGI(TAG, "connected to %s", ssid.c_str());
    return true;
  }
  ESP_LOGW(TAG, "connect failed / timeout for %s", ssid.c_str());
  s_sta_connected = false;
  return false;
}

bool EspWifi::connected() const { return s_sta_connected; }
