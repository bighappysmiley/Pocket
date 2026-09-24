#pragma once
#include <cstdint>
#include <string>

namespace pocket::board {

/** Mount LittleFS data partition at /littlefs (2 MiB). */
bool littlefs_mount();

bool littlefs_mounted();

/** Free bytes on a mounted VFS root ("/sdcard" or "/littlefs"). */
uint64_t fs_free_bytes(const char* root);

}  // namespace pocket::board
