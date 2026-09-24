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

#ifndef POCKET_DEVICE_API_KEY
#define POCKET_DEVICE_API_KEY "dev-device-api-key"
#endif

/** Spec Part D §4.1 alphabet (no 0/O/1/I). */
inline constexpr const char* kPairCodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";

std::string companion_pair_url(std::string_view code);
std::string companion_wifi_setup_url();
std::string companion_download_url();
std::string companion_display_origin();
std::string generate_pair_code();

}  // namespace pocket
