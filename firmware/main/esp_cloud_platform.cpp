#include "esp_cloud_platform.hpp"

#include "pocket/cloud_client.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"

namespace {

constexpr const char* TAG = "pocket_cloud";

struct HttpBuf {
  std::string body;
};

esp_err_t http_event(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA && evt->user_data && evt->data && evt->data_len > 0) {
    auto* buf = static_cast<HttpBuf*>(evt->user_data);
    buf->body.append(static_cast<const char*>(evt->data), evt->data_len);
  }
  return ESP_OK;
}

bool http_request(const char* method, const std::string& url, const char* auth_bearer,
                  const std::string& body, int& status_out, std::string& response_out,
                  int timeout_ms = 12000, const char* accept = "application/json",
                  const char* extra_header_name = nullptr, const char* extra_header_value = nullptr) {
  HttpBuf buf;
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.method = (std::strcmp(method, "POST") == 0) ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  cfg.timeout_ms = timeout_ms;
  cfg.event_handler = http_event;
  cfg.user_data = &buf;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;

  if (auth_bearer && auth_bearer[0]) {
    // Neon Functions intercepts Authorization: Bearer (platform auth). Use x-device-key only.
    esp_http_client_set_header(client, "x-device-key", auth_bearer);
  }
  if (accept && accept[0]) {
    esp_http_client_set_header(client, "Accept", accept);
  }
  if (extra_header_name && extra_header_value) {
    esp_http_client_set_header(client, extra_header_name, extra_header_value);
  }
  if (!body.empty()) {
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, body.c_str(), body.size());
  }

  esp_err_t err = esp_http_client_perform(client);
  status_out = esp_http_client_get_status_code(client);
  response_out = std::move(buf.body);
  esp_http_client_cleanup(client);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "%s %s failed: %s", method, url.c_str(), esp_err_to_name(err));
    return false;
  }
  return true;
}

std::string json_string_field(const std::string& json, const char* key) {
  const std::string needle = std::string("\"") + key + "\"";
  size_t p = json.find(needle);
  if (p == std::string::npos) return {};
  p = json.find(':', p + needle.size());
  if (p == std::string::npos) return {};
  p = json.find('"', p + 1);
  if (p == std::string::npos) return {};
  size_t end = json.find('"', p + 1);
  if (end == std::string::npos) return {};
  return json.substr(p + 1, end - p - 1);
}

std::string mint_pair_code() {
  std::string code;
  code.reserve(8);
  for (int i = 0; i < 8; ++i) {
    const uint32_t r = esp_random();
    code.push_back(pocket::kPairCodeAlphabet[r % 32]);
  }
  return code;
}

}  // namespace

std::string EspCloud::create_pair_session(const std::string& device_id) {
  const std::string code = mint_pair_code();
  if (device_id.empty()) {
    ESP_LOGW(TAG, "create_pair_session: empty device_id");
    return {};
  }

  // Let Cloud set TTL — device RTC may be unsynced before SNTP (1970 expires_at breaks Node API).
  char body[280];
  std::snprintf(body, sizeof(body), "{\"device_id\":\"%s\",\"code_public\":\"%s\"}", device_id.c_str(),
                code.c_str());

  const std::string url = std::string(POCKET_CLOUD_BASE) + "/v1/pair/sessions";
  int status = 0;
  std::string resp;
  if (!http_request("POST", url, POCKET_DEVICE_API_KEY, body, status, resp) ||
      (status != 200 && status != 201)) {
    ESP_LOGW(TAG, "pair create HTTP %d resp=%s", status, resp.c_str());
    return {};
  }
  ESP_LOGI(TAG, "pair session created code=%s", code.c_str());
  return code;
}

std::string EspCloud::pair_status(const std::string& code) {
  if (code.empty()) return "expired";
  const std::string url = std::string(POCKET_CLOUD_BASE) + "/v1/pair/sessions/" + code;
  int status = 0;
  std::string resp;
  if (!http_request("GET", url, nullptr, {}, status, resp) || status != 200) {
    ESP_LOGW(TAG, "pair status HTTP %d", status);
    return "pending";
  }
  const std::string st = json_string_field(resp, "status");
  if (st == "claimed" || st == "expired" || st == "pending") return st;
  return "pending";
}

std::string EspCloud::music_list_json(const std::string& device_id, bool sd_present) {
  if (device_id.empty()) return "[]";
  std::string url = std::string(POCKET_CLOUD_BASE) + "/v1/device/music?device_id=" + device_id;
  if (sd_present) url += "&sd=1";
  int status = 0;
  std::string resp;
  if (!http_request("GET", url, POCKET_DEVICE_API_KEY, {}, status, resp, 20000, "application/json",
                    "x-pocket-sd", sd_present ? "1" : "0") ||
      status != 200) {
    ESP_LOGW(TAG, "music list HTTP %d", status);
    return {};
  }
  // Prefer the tracks array payload for the device parser.
  const size_t arr = resp.find('[');
  if (arr == std::string::npos) return "[]";
  return resp.substr(arr);
}

std::vector<uint8_t> EspCloud::music_download(const std::string& device_id, const std::string& track_id) {
  std::vector<uint8_t> out;
  if (device_id.empty() || track_id.empty()) return out;
  const std::string url = std::string(POCKET_CLOUD_BASE) + "/v1/music/" + track_id +
                          "/audio?device_id=" + device_id;
  int status = 0;
  std::string resp;
  if (!http_request("GET", url, POCKET_DEVICE_API_KEY, {}, status, resp, 60000, "audio/wav,application/octet-stream,*/*",
                    "x-device-id", device_id.c_str()) ||
      status != 200) {
    ESP_LOGW(TAG, "music download HTTP %d id=%s", status, track_id.c_str());
    return out;
  }
  out.assign(resp.begin(), resp.end());
  return out;
}

std::string EspCloud::firmware_latest_json() {
  const std::string url = std::string(POCKET_CLOUD_BASE) + "/v1/firmware/latest";
  int status = 0;
  std::string resp;
  if (!http_request("GET", url, nullptr, {}, status, resp, 20000) || status != 200) {
    ESP_LOGW(TAG, "firmware latest HTTP %d", status);
    return {};
  }
  return resp;
}
