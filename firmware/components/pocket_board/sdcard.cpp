#include "pocket_board/sdcard.hpp"
#include "pocket_board/pins.hpp"

#include <cctype>
#include <cstring>
#include <dirent.h>
#include <string>
#include <strings.h>

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_sd";
constexpr const char* kMount = "/sdcard";

sdmmc_card_t* card_ = nullptr;
bool mounted_ = false;

bool ends_with_ci(const char* name, const char* ext) {
  const size_t n = std::strlen(name);
  const size_t e = std::strlen(ext);
  if (n < e) return false;
  return strcasecmp(name + (n - e), ext) == 0;
}

bool name_is_firmware_risk(const char* name) {
  if (!name || !*name) return false;
  if (ends_with_ci(name, ".bin") || ends_with_ci(name, ".elf") || ends_with_ci(name, ".hex")) return true;
  if (strcasecmp(name, "bootloader") == 0 || strcasecmp(name, "bootloader.bin") == 0) return true;
  if (strncasecmp(name, "partition", 9) == 0) return true;
  if (strncasecmp(name, "ota", 3) == 0) return true;
  if (strncasecmp(name, "firmware", 8) == 0) return true;
  if (strncasecmp(name, "pocket", 6) == 0 && ends_with_ci(name, ".bin")) return true;
  if (strcasecmp(name, "flasher_args.json") == 0) return true;
  return false;
}

bool name_is_media(const char* name) {
  static const char* kExt[] = {".mp3", ".wav", ".flac", ".aac", ".m4a", ".ogg", ".wma",
                               ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
                               ".mp4", ".mov", ".avi", ".mkv", nullptr};
  for (int i = 0; kExt[i]; ++i) {
    if (ends_with_ci(name, kExt[i])) return true;
  }
  return false;
}

bool mount_internal(bool format_if_failed) {
  if (mounted_) return true;

  esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
  mount_config.format_if_mount_failed = format_if_failed;
  mount_config.max_files = 5;
  mount_config.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = static_cast<gpio_num_t>(kPinSdClk);
  slot.cmd = static_cast<gpio_num_t>(kPinSdCmd);
  slot.d0 = static_cast<gpio_num_t>(kPinSdD0);
  slot.d1 = static_cast<gpio_num_t>(kPinSdD1);
  slot.d2 = static_cast<gpio_num_t>(kPinSdD2);
  slot.d3 = static_cast<gpio_num_t>(kPinSdD3);

  esp_err_t ret = esp_vfs_fat_sdmmc_mount(kMount, &host, &slot, &mount_config, &card_);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(ret));
    card_ = nullptr;
    mounted_ = false;
    return false;
  }
  mounted_ = true;
  ESP_LOGI(TAG, "mounted at %s", kMount);
  return true;
}

}  // namespace

bool sd_probe() { return mount_internal(false); }

bool sd_present() { return mounted_ || mount_internal(false); }

SdContentKind sd_classify() {
  if (!sd_present()) return SdContentKind::Absent;

  DIR* dir = opendir(kMount);
  if (!dir) return SdContentKind::Unknown;

  bool any = false;
  bool media = false;
  bool risk = false;
  while (struct dirent* ent = readdir(dir)) {
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    any = true;
    if (name_is_firmware_risk(ent->d_name)) risk = true;
    if (name_is_media(ent->d_name)) media = true;
    // Peek one level into directories named like firmware
    if (ent->d_type == DT_DIR && name_is_firmware_risk(ent->d_name)) risk = true;
  }
  closedir(dir);

  if (!any) return SdContentKind::Empty;
  if (risk) return SdContentKind::FirmwareRisk;
  if (media) return SdContentKind::Media;
  return SdContentKind::Unknown;
}

bool sd_erase() {
  if (!mounted_ && !mount_internal(false)) {
    // Try format-on-mount for blank/corrupt cards
    if (!mount_internal(true)) return false;
    return true;
  }
  if (!card_) return false;
  esp_err_t err = esp_vfs_fat_sdcard_format(kMount, card_);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "format failed: %s", esp_err_to_name(err));
    // Remount with format
    sd_unmount();
    return mount_internal(true);
  }
  ESP_LOGI(TAG, "card erased");
  return true;
}

void sd_unmount() {
  if (!mounted_) return;
  esp_vfs_fat_sdcard_unmount(kMount, card_);
  card_ = nullptr;
  mounted_ = false;
  ESP_LOGI(TAG, "unmounted");
}

}  // namespace pocket::board
