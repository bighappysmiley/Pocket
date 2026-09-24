#include "esp_config_store.hpp"

#include "pocket_board/sdcard.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <vector>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace {

constexpr const char* TAG = "pocket_cfg";
constexpr const char* kNvsNs = "pocket";
constexpr const char* kNvsKey = "devcfg";
constexpr const char* kSdDir = "/sdcard/pocket";
constexpr const char* kSdPath = "/sdcard/pocket/config.bin";
/** Leave headroom on SD so the card stays usable for media / system files. */
constexpr uint64_t kMinSdFreeBytes = 512 * 1024;

bool nvs_load_blob(std::vector<uint8_t>& out) {
  nvs_handle_t h = 0;
  if (nvs_open(kNvsNs, NVS_READONLY, &h) != ESP_OK) return false;
  size_t len = 0;
  esp_err_t err = nvs_get_blob(h, kNvsKey, nullptr, &len);
  if (err != ESP_OK || len == 0 || len > 4096) {
    nvs_close(h);
    return false;
  }
  out.resize(len);
  err = nvs_get_blob(h, kNvsKey, out.data(), &len);
  nvs_close(h);
  if (err != ESP_OK) {
    out.clear();
    return false;
  }
  out.resize(len);
  return true;
}

bool nvs_save_blob(const std::vector<uint8_t>& blob) {
  if (blob.empty() || blob.size() > 4096) return false;
  nvs_handle_t h = 0;
  if (nvs_open(kNvsNs, NVS_READWRITE, &h) != ESP_OK) return false;
  esp_err_t err = nvs_set_blob(h, kNvsKey, blob.data(), blob.size());
  if (err == ESP_OK) err = nvs_commit(h);
  nvs_close(h);
  return err == ESP_OK;
}

bool sd_has_space(uint64_t need) {
  struct statvfs st {};
  if (statvfs("/sdcard", &st) != 0) return false;
  const uint64_t free_bytes = static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
  return free_bytes >= (need + kMinSdFreeBytes);
}

bool sd_load_blob(std::vector<uint8_t>& out) {
  if (!pocket::board::sd_present()) return false;
  FILE* f = std::fopen(kSdPath, "rb");
  if (!f) return false;
  if (std::fseek(f, 0, SEEK_END) != 0) {
    std::fclose(f);
    return false;
  }
  const long sz = std::ftell(f);
  if (sz <= 0 || sz > 4096) {
    std::fclose(f);
    return false;
  }
  if (std::fseek(f, 0, SEEK_SET) != 0) {
    std::fclose(f);
    return false;
  }
  out.resize(static_cast<size_t>(sz));
  const size_t n = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  if (n != out.size()) {
    out.clear();
    return false;
  }
  return true;
}

bool sd_save_blob(const std::vector<uint8_t>& blob) {
  if (blob.empty()) return false;
  if (!pocket::board::sd_probe()) return false;
  if (!sd_has_space(blob.size())) {
    ESP_LOGW(TAG, "SD free space too low — keeping NVS only");
    return false;
  }
  // Ensure /sdcard/pocket exists
  struct stat st {};
  if (stat(kSdDir, &st) != 0) {
    if (mkdir(kSdDir, 0755) != 0) {
      ESP_LOGW(TAG, "mkdir %s failed", kSdDir);
      return false;
    }
  }
  FILE* f = std::fopen(kSdPath, "wb");
  if (!f) return false;
  const size_t n = std::fwrite(blob.data(), 1, blob.size(), f);
  std::fclose(f);
  return n == blob.size();
}

}  // namespace

pocket::DeviceConfig EspPersistentConfigStore::load() {
  pocket::DeviceConfig cfg;
  std::vector<uint8_t> blob;

  // Prefer SD mirror when present (user may have moved the card), else NVS.
  if (sd_load_blob(blob)) {
    if (pocket::unpack_device_config(blob.data(), blob.size(), cfg)) {
      ESP_LOGI(TAG, "loaded config from SD");
      return cfg;
    }
  }
  blob.clear();
  if (nvs_load_blob(blob) && pocket::unpack_device_config(blob.data(), blob.size(), cfg)) {
    ESP_LOGI(TAG, "loaded config from NVS");
    return cfg;
  }
  ESP_LOGI(TAG, "no persisted config — defaults");
  return cfg;
}

void EspPersistentConfigStore::save(const pocket::DeviceConfig& cfg) {
  std::vector<uint8_t> blob;
  if (!pocket::pack_device_config(cfg, blob)) {
    ESP_LOGW(TAG, "pack failed");
    return;
  }
  if (!nvs_save_blob(blob)) {
    ESP_LOGW(TAG, "NVS save failed");
  } else {
    ESP_LOGI(TAG, "saved config to NVS (%u bytes)", static_cast<unsigned>(blob.size()));
  }
  if (sd_save_blob(blob)) {
    ESP_LOGI(TAG, "mirrored config to SD");
  }
}
