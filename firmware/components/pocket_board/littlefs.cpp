#include "pocket_board/littlefs.hpp"

#include <cstring>
#include <string>

#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_vfs_fat.h"

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_lfs";
constexpr const char* kMount = "/littlefs";
constexpr const char* kPartLabel = "littlefs";
bool mounted_ = false;

}  // namespace

bool littlefs_mount() {
  if (mounted_) return true;
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kMount;
  conf.partition_label = kPartLabel;
  conf.format_if_mount_failed = true;
  conf.dont_mount = false;
  esp_err_t err = esp_vfs_littlefs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(err));
    return false;
  }
  mounted_ = true;
  ESP_LOGI(TAG, "mounted at %s", kMount);
  return true;
}

bool littlefs_mounted() { return mounted_; }

uint64_t fs_free_bytes(const char* root) {
  if (!root || !*root) return 0;

  // Newlib on ESP-IDF has no sys/statvfs.h — use component APIs.
  if (std::strncmp(root, "/littlefs", 9) == 0) {
    size_t total = 0, used = 0;
    if (esp_littlefs_info(kPartLabel, &total, &used) != ESP_OK) return 0;
    if (used >= total) return 0;
    return static_cast<uint64_t>(total - used);
  }

  if (std::strncmp(root, "/sdcard", 7) == 0) {
    uint64_t total = 0, free_b = 0;
    if (esp_vfs_fat_info("/sdcard", &total, &free_b) != ESP_OK) return 0;
    return free_b;
  }

  return 0;
}

}  // namespace pocket::board
