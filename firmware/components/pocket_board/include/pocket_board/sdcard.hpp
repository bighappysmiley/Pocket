#pragma once
#include <cstdint>

namespace pocket::board {

enum class SdContentKind : uint8_t {
  Absent = 0,
  Empty,
  Media,          // audio/photos — allow eject without erase
  FirmwareRisk,   // bins / OTA / bootloader — must erase to continue
  Unknown,
};

/** Try mount `/sdcard`. Returns true when a card is mounted. */
bool sd_probe();

bool sd_present();

SdContentKind sd_classify();

/** Wipe FAT and remount empty. */
bool sd_erase();

/** Unmount (call before physical eject). */
void sd_unmount();

}  // namespace pocket::board
