#include "esp_wifi_platform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr const char* TAG = "pocket_wifi";
constexpr int kMaxAps = 24;
constexpr EventBits_t kBitConnected = BIT0;
constexpr EventBits_t kBitFail = BIT1;

EventGroupHandle_t s_wifi_events = nullptr;
SemaphoreHandle_t s_prov_mu = nullptr;
bool s_wifi_ready = false;
bool s_sta_connected = false;
bool s_ap_netif_ready = false;
bool s_provision_active = false;
/** True only while waiting for GOT_IP / terminal fail after esp_wifi_connect(). */
bool s_sta_connecting = false;
httpd_handle_t s_httpd = nullptr;
char s_ap_ssid[33] = {};
char s_ap_pass[17] = {};  // WPA2 short password shown on device (8 chars + NUL)
char s_preferred_ssid[33] = {};
char s_cred_ssid[33] = {};
char s_cred_pass[65] = {};
bool s_creds_ready = false;

void wifi_event_handler(void* /*arg*/, esp_event_base_t base, int32_t id, void* data) {
  if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
    s_sta_connected = false;
    const auto* disc = static_cast<const wifi_event_sta_disconnected_t*>(data);
    const uint8_t reason = disc ? disc->reason : 0;
    ESP_LOGW(TAG, "STA disconnected reason=%u connecting=%d", static_cast<unsigned>(reason),
             s_sta_connecting ? 1 : 0);
    // Ignore disconnects from our own esp_wifi_disconnect() / mode flips before connect().
    if (!s_sta_connecting) return;
    if (s_wifi_events) xEventGroupSetBits(s_wifi_events, kBitFail);
  } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
    s_sta_connected = true;
    s_sta_connecting = false;
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
  s_prov_mu = xSemaphoreCreateMutex();
  s_wifi_ready = true;
  ESP_LOGI(TAG, "STA ready");
  return true;
}

void lock_prov() {
  if (s_prov_mu) xSemaphoreTake(s_prov_mu, portMAX_DELAY);
}
void unlock_prov() {
  if (s_prov_mu) xSemaphoreGive(s_prov_mu);
}

std::vector<std::string> scan_networks_locked() {
  std::vector<std::string> out;
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
    if (out.size() >= 12) break;
  }
  return out;
}

void json_escape(const std::string& in, std::string& out) {
  out.clear();
  out.reserve(in.size() + 8);
  for (char c : in) {
    if (c == '"' || c == '\\') {
      out.push_back('\\');
      out.push_back(c);
    } else if (static_cast<unsigned char>(c) < 0x20) {
      char buf[8];
      std::snprintf(buf, sizeof(buf), "\\u%04x", c);
      out += buf;
    } else {
      out.push_back(c);
    }
  }
}

bool extract_json_string(const char* body, const char* key, char* dest, size_t dest_len) {
  if (!body || !key || !dest || dest_len < 2) return false;
  std::string needle = std::string("\"") + key + "\"";
  const char* p = std::strstr(body, needle.c_str());
  if (!p) return false;
  p = std::strchr(p + needle.size(), ':');
  if (!p) return false;
  ++p;
  while (*p == ' ' || *p == '\t') ++p;
  if (*p != '"') return false;
  ++p;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < dest_len) {
    if (*p == '\\' && p[1]) {
      ++p;
      dest[i++] = *p++;
    } else {
      dest[i++] = *p++;
    }
  }
  dest[i] = 0;
  return true;
}

void add_cors(httpd_req_t* req) {
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
}

esp_err_t handle_options(httpd_req_t* req) {
  add_cors(req);
  httpd_resp_set_status(req, "204 No Content");
  return httpd_resp_send(req, nullptr, 0);
}

esp_err_t handle_status(httpd_req_t* req) {
  add_cors(req);
  httpd_resp_set_type(req, "application/json");
  char preferred[33] = {};
  char ap[33] = {};
  lock_prov();
  std::strncpy(preferred, s_preferred_ssid, sizeof(preferred) - 1);
  std::strncpy(ap, s_ap_ssid, sizeof(ap) - 1);
  const bool ready = s_creds_ready;
  unlock_prov();

  std::string pref_esc, ap_esc;
  json_escape(preferred, pref_esc);
  json_escape(ap, ap_esc);
  char buf[384];
  std::snprintf(buf, sizeof(buf),
                "{\"ok\":true,\"provisioning\":true,\"ap_ssid\":\"%s\",\"preferred_ssid\":\"%s\",\"credentials_received\":%s}",
                ap_esc.c_str(), pref_esc.c_str(), ready ? "true" : "false");
  return httpd_resp_send(req, buf, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_scan(httpd_req_t* req) {
  add_cors(req);
  httpd_resp_set_type(req, "application/json");
  auto nets = scan_networks_locked();
  std::string body = "{\"ok\":true,\"networks\":[";
  for (size_t i = 0; i < nets.size(); ++i) {
    std::string esc;
    json_escape(nets[i], esc);
    if (i) body += ',';
    body += '"';
    body += esc;
    body += '"';
  }
  body += "]}";
  return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t handle_wifi_post(httpd_req_t* req) {
  add_cors(req);
  if (req->content_len <= 0 || req->content_len > 512) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "{\"ok\":false,\"message\":\"Bad request\"}", HTTPD_RESP_USE_STRLEN);
  }
  std::vector<char> body(static_cast<size_t>(req->content_len) + 1);
  int r = httpd_req_recv(req, body.data(), req->content_len);
  if (r <= 0) {
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "{\"ok\":false,\"message\":\"Bad request\"}", HTTPD_RESP_USE_STRLEN);
  }
  body[static_cast<size_t>(r)] = 0;

  char ssid[33] = {};
  char pass[65] = {};
  if (!extract_json_string(body.data(), "ssid", ssid, sizeof(ssid)) || !ssid[0]) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "400 Bad Request");
    return httpd_resp_send(req, "{\"ok\":false,\"message\":\"Choose a network\"}", HTTPD_RESP_USE_STRLEN);
  }
  extract_json_string(body.data(), "password", pass, sizeof(pass));

  lock_prov();
  std::strncpy(s_cred_ssid, ssid, sizeof(s_cred_ssid) - 1);
  std::strncpy(s_cred_pass, pass, sizeof(s_cred_pass) - 1);
  s_creds_ready = true;
  unlock_prov();
  ESP_LOGI(TAG, "provision credentials received for %s", ssid);

  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, "{\"ok\":true,\"message\":\"Connecting Pocket to Wi-Fi\"}", HTTPD_RESP_USE_STRLEN);
}

static const char kPortalHtml[] = R"HTML(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"/><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>Pocket Wi-Fi</title>
<style>
body{font-family:system-ui,sans-serif;margin:1.25rem;max-width:28rem;color:#111;background:#f7f5f2}
h1{font-size:1.35rem;margin:0 0 .35rem}p{color:#555;line-height:1.4}
label{display:block;margin:.85rem 0 .35rem;font-weight:600}
input,select,button{width:100%;box-sizing:border-box;font:inherit;padding:.7rem .8rem;border-radius:8px;border:1px solid #ccc}
button{background:#111;color:#fff;border:none;margin-top:1rem;font-weight:600}
.msg{margin-top:1rem;padding:.75rem;border-radius:8px;background:#eee}
</style></head><body>
<h1>Set up Pocket Wi-Fi</h1>
<p>Enter your <strong>home Wi‑Fi password</strong> here. When Pocket connects, your phone will show a pairing code next.</p>
<label for="ssid">Network</label>
<select id="ssid"></select>
<label for="password">Password</label>
<input id="password" type="password" autocomplete="current-password"/>
<button id="go" type="button">Connect Pocket</button>
<div class="msg" id="msg">Looking for networks…</div>
<script>
const msg=document.getElementById('msg');
const sel=document.getElementById('ssid');
async function load(){
  try{
    const st=await fetch('/api/status').then(r=>r.json());
    const sc=await fetch('/api/scan').then(r=>r.json());
    sel.innerHTML='';
    const nets=sc.networks||[];
    if(!nets.length){msg.textContent='No networks found. Move closer to your router, then reload.';return;}
    for(const n of nets){
      const o=document.createElement('option');o.value=n;o.textContent=n;
      if(st.preferred_ssid&&st.preferred_ssid===n)o.selected=true;
      sel.appendChild(o);
    }
    msg.textContent='Ready — enter the Wi‑Fi password, then Connect Pocket.';
  }catch(e){msg.textContent='Could not reach Pocket. Stay joined to the Pocket Wi‑Fi network.';}
}
document.getElementById('go').onclick=async()=>{
  msg.textContent='Sending…';
  try{
    const body={ssid:sel.value,password:document.getElementById('password').value};
    const res=await fetch('/api/wifi',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
    const j=await res.json();
    if(!res.ok){msg.textContent=j.message||'Could not send.';return;}
    msg.textContent='Sent. Rejoin your home Wi‑Fi, open the Pocket app, then enter the pairing code shown on Pocket.';
  }catch(e){msg.textContent='Send failed. Stay on the Pocket Wi‑Fi and try again.';}
};
load();
</script></body></html>)HTML";

esp_err_t handle_root(httpd_req_t* req) {
  add_cors(req);
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, kPortalHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t handle_captive(httpd_req_t* req) {
  httpd_resp_set_status(req, "302 Found");
  httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
  return httpd_resp_send(req, nullptr, 0);
}

void stop_httpd() {
  if (s_httpd) {
    httpd_stop(s_httpd);
    s_httpd = nullptr;
  }
}

bool start_httpd() {
  stop_httpd();
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.lru_purge_enable = true;
  config.max_uri_handlers = 12;
  if (httpd_start(&s_httpd, &config) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    s_httpd = nullptr;
    return false;
  }

  const httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = handle_root, .user_ctx = nullptr},
      {.uri = "/api/status", .method = HTTP_GET, .handler = handle_status, .user_ctx = nullptr},
      {.uri = "/api/scan", .method = HTTP_GET, .handler = handle_scan, .user_ctx = nullptr},
      {.uri = "/api/wifi", .method = HTTP_POST, .handler = handle_wifi_post, .user_ctx = nullptr},
      {.uri = "/api/wifi", .method = HTTP_OPTIONS, .handler = handle_options, .user_ctx = nullptr},
      {.uri = "/api/status", .method = HTTP_OPTIONS, .handler = handle_options, .user_ctx = nullptr},
      {.uri = "/api/scan", .method = HTTP_OPTIONS, .handler = handle_options, .user_ctx = nullptr},
      {.uri = "/generate_204", .method = HTTP_GET, .handler = handle_captive, .user_ctx = nullptr},
      {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handle_captive, .user_ctx = nullptr},
      {.uri = "/connecttest.txt", .method = HTTP_GET, .handler = handle_captive, .user_ctx = nullptr},
  };
  for (const auto& r : routes) {
    httpd_register_uri_handler(s_httpd, &r);
  }
  return true;
}

void build_ap_ssid(char* out, size_t out_len) {
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  std::snprintf(out, out_len, "Pocket-%02X%02X", mac[4], mac[5]);
}

/** Short WPA2 password (8 chars) — easy to type from the e-ink display. Always filled. */
void build_ap_password(char* out, size_t out_len) {
  // Unambiguous alphabet (no 0/O, 1/I/L). WPA2 requires ≥8 characters.
  static constexpr char kAlphabet[] = "23456789ABCDEFGHJKMNPQRSTUVWXYZ";
  static constexpr size_t kAlphaN = sizeof(kAlphabet) - 1;
  static constexpr size_t kLen = 8;
  if (!out || out_len <= kLen) {
    if (out && out_len) out[0] = 0;
    return;
  }
  uint8_t mac[6] = {};
  esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
  for (size_t i = 0; i < kLen; ++i) {
    const uint32_t r = esp_random();
    const uint32_t mix = r ^ (static_cast<uint32_t>(mac[i % 6]) << (i * 3)) ^ static_cast<uint32_t>(i * 17u);
    out[i] = kAlphabet[mix % kAlphaN];
  }
  out[kLen] = 0;
}

}  // namespace

// Defined in app_main.cpp — starts SNTP after STA joins.
extern "C" void pocket_on_wifi_connected(void);

std::vector<std::string> EspWifi::scan() {
  if (!wifi_ensure()) return {};
  return scan_networks_locked();
}

bool EspWifi::connect(const std::string& ssid, const std::string& pass) {
  if (!wifi_ensure()) return false;
  if (ssid.empty() || ssid.size() > 31) return false;
  if (pass.size() > 63) return false;

  // Do not treat SoftAP teardown / disconnect as a connect failure.
  s_sta_connecting = false;
  stop_provision();

  wifi_config_t cfg = {};
  std::memcpy(cfg.sta.ssid, ssid.c_str(), ssid.size());
  std::memcpy(cfg.sta.password, pass.c_str(), pass.size());
  // Accept WPA/WPA2 (and WPA3-transition APs that still offer WPA2).
  cfg.sta.threshold.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA_WPA2_PSK;
  cfg.sta.pmf_cfg.capable = true;
  cfg.sta.pmf_cfg.required = false;

  if (s_wifi_events) xEventGroupClearBits(s_wifi_events, kBitConnected | kBitFail);
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, &cfg));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_disconnect());
  // Let the disconnect event settle, then clear any stale fail bit before connecting.
  vTaskDelay(pdMS_TO_TICKS(100));
  if (s_wifi_events) xEventGroupClearBits(s_wifi_events, kBitConnected | kBitFail);

  s_sta_connecting = true;
  esp_err_t err = esp_wifi_connect();
  if (err != ESP_OK) {
    s_sta_connecting = false;
    ESP_LOGW(TAG, "connect: %s", esp_err_to_name(err));
    return false;
  }

  EventBits_t bits =
      xEventGroupWaitBits(s_wifi_events, kBitConnected | kBitFail, pdTRUE, pdFALSE, pdMS_TO_TICKS(20000));
  s_sta_connecting = false;
  if (bits & kBitConnected) {
    ESP_LOGI(TAG, "connected to %s", ssid.c_str());
    pocket_on_wifi_connected();
    return true;
  }
  ESP_LOGW(TAG, "connect failed / timeout for %s", ssid.c_str());
  s_sta_connected = false;
  return false;
}

bool EspWifi::connected() const { return s_sta_connected; }

bool EspWifi::start_provision(const std::string& preferred_ssid, std::string* ap_ssid_out,
                              std::string* ap_pass_out) {
  if (!wifi_ensure()) return false;
  stop_httpd();

  if (!s_ap_netif_ready) {
    esp_netif_create_default_wifi_ap();
    s_ap_netif_ready = true;
  }

  build_ap_ssid(s_ap_ssid, sizeof(s_ap_ssid));
  // Reuse SoftAP password across failed STA retries so the phone does not need to rejoin.
  if (!s_ap_pass[0]) {
    build_ap_password(s_ap_pass, sizeof(s_ap_pass));
  }
  lock_prov();
  std::memset(s_preferred_ssid, 0, sizeof(s_preferred_ssid));
  if (!preferred_ssid.empty() && preferred_ssid.size() < sizeof(s_preferred_ssid)) {
    std::strncpy(s_preferred_ssid, preferred_ssid.c_str(), sizeof(s_preferred_ssid) - 1);
  }
  s_creds_ready = false;
  std::memset(s_cred_ssid, 0, sizeof(s_cred_ssid));
  std::memset(s_cred_pass, 0, sizeof(s_cred_pass));
  unlock_prov();

  wifi_config_t ap = {};
  std::strncpy(reinterpret_cast<char*>(ap.ap.ssid), s_ap_ssid, sizeof(ap.ap.ssid) - 1);
  ap.ap.ssid_len = static_cast<uint8_t>(std::strlen(s_ap_ssid));
  std::strncpy(reinterpret_cast<char*>(ap.ap.password), s_ap_pass, sizeof(ap.ap.password) - 1);
  ap.ap.channel = 1;
  ap.ap.max_connection = 4;
  ap.ap.authmode = WIFI_AUTH_WPA2_PSK;
  ap.ap.ssid_hidden = 0;

  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_APSTA));
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, &ap));
  // wifi already started in wifi_ensure

  if (!start_httpd()) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
    s_provision_active = false;
    return false;
  }

  s_provision_active = true;
  if (ap_ssid_out) *ap_ssid_out = s_ap_ssid;
  if (ap_pass_out) *ap_pass_out = s_ap_pass;
  ESP_LOGI(TAG, "SoftAP provision active: %s pass_len=%u", s_ap_ssid,
           static_cast<unsigned>(std::strlen(s_ap_pass)));
  return true;
}

void EspWifi::stop_provision() {
  stop_httpd();
  lock_prov();
  s_provision_active = false;
  unlock_prov();
  if (s_wifi_ready) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
  }
}

bool EspWifi::take_provision_credentials(std::string* ssid, std::string* password) {
  lock_prov();
  if (!s_creds_ready) {
    unlock_prov();
    return false;
  }
  if (ssid) *ssid = s_cred_ssid;
  if (password) *password = s_cred_pass;
  s_creds_ready = false;
  unlock_prov();
  return true;
}

std::string EspWifi::provision_ap_ssid() const { return s_ap_ssid; }

std::string EspWifi::provision_ap_password() const { return s_ap_pass; }

bool EspWifi::provisioning() const { return s_provision_active; }
