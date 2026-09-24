#include "esp_cloud_platform.hpp"

#include "pocket/cloud_client.hpp"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

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
                  const std::string& body, int& status_out, std::string& response_out) {
  HttpBuf buf;
  esp_http_client_config_t cfg = {};
  cfg.url = url.c_str();
  cfg.method = (std::strcmp(method, "POST") == 0) ? HTTP_METHOD_POST : HTTP_METHOD_GET;
  cfg.timeout_ms = 12000;
  cfg.event_handler = http_event;
  cfg.user_data = &buf;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) return false;

  if (auth_bearer && auth_bearer[0]) {
    // Neon Functions intercepts Authorization: Bearer (platform auth). Use x-device-key only.
    esp_http_client_set_header(client, "x-device-key", auth_bearer);
  }
  esp_http_client_set_header(client, "Accept", "application/json");
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
    return code;
  }

  const time_t expires = time(nullptr) + 10 * 60;
  struct tm tm_utc {};
  gmtime_r(&expires, &tm_utc);
  char expires_iso[40];
  std::strftime(expires_iso, sizeof(expires_iso), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

  char body[320];
  std::snprintf(body, sizeof(body),
                "{\"device_id\":\"%s\",\"code_public\":\"%s\",\"expires_at\":\"%s\"}", device_id.c_str(),
                code.c_str(), expires_iso);

  const std::string url = std::string(POCKET_CLOUD_BASE) + "/v1/pair/sessions";
  int status = 0;
  std::string resp;
  if (!http_request("POST", url, POCKET_DEVICE_API_KEY, body, status, resp) ||
      (status != 200 && status != 201)) {
    ESP_LOGW(TAG, "pair create HTTP %d resp=%s", status, resp.c_str());
  } else {
    ESP_LOGI(TAG, "pair session created code=%s", code.c_str());
  }
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
