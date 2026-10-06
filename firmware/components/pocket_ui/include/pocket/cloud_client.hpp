#pragma once
#include <string>
#include <string_view>

namespace pocket {

/** PWA origin used in pairing QR payloads (no trailing slash). */
#ifndef POCKET_PWA_ORIGIN
#define POCKET_PWA_ORIGIN "https://bighappysmiley.github.io/Pocket"
#endif

/** Pocket Cloud API origin (no trailing slash). */
#ifndef POCKET_CLOUD_BASE
#define POCKET_CLOUD_BASE "https://br-super-hill-b40yvyrj-api.compute.c-6.us-east-2.aws.neon.tech"
#endif

/**
 * Static OTA discovery mirrors (no Neon cold-start). Tried after Cloud if Cloud fails.
 * Pages is published by Deploy; GitHub release is the firmware-latest asset.
 */
#ifndef POCKET_FW_MANIFEST_PAGES
#define POCKET_FW_MANIFEST_PAGES "https://bighappysmiley.github.io/Pocket/firmware-manifest.json"
#endif
#ifndef POCKET_FW_MANIFEST_RELEASE
#define POCKET_FW_MANIFEST_RELEASE \
  "https://github.com/bighappysmiley/Pocket/releases/download/firmware-latest/firmware-manifest.json"
#endif

#ifndef POCKET_DEVICE_API_KEY
#define POCKET_DEVICE_API_KEY "dev-device-api-key"
#endif

/** Spec Part D §4.1 alphabet (no 0/O/1/I). */
inline constexpr const char* kPairCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string companion_pair_url(std::string_view code);
/** Unified Link flow in the app (Wi‑Fi setup + pairing). */
std::string companion_link_url();
/** SoftAP captive portal — use while phone is joined to Pocket-XXXX. */
std::string softap_portal_url();
std::string companion_wifi_setup_url();
std::string companion_download_url();
std::string companion_display_origin();
std::string generate_pair_code();

}  // namespace pocket
