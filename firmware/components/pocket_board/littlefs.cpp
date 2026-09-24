#include "pocket_board/littlefs.hpp"

#include <sys/stat.h>
#include <sys/statvfs.h>

#include "esp_log.h"
#include "esp_littlefs.h"

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_lfs";
constexpr const char* kMount = "/littlefs";
bool mounted_ = false;

}  // namespace

bool littlefs_mount() {
  if (mounted_) return true;
  esp_vfs_littlefs_conf_t conf = {};
  conf.base_path = kMount;
  conf.partition_label = "littlefs";
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
  if (!root) return 0;
  struct statvfs st {};
  if (statvfs(root, &st) != 0) return 0;
  return static_cast<uint64_t>(st.f_bavail) * static_cast<uint64_t>(st.f_frsize);
}

}  // namespace pocket::board
