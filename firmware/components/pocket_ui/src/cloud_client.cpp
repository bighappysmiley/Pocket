#include "pocket/cloud_client.hpp"
#include <cctype>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>

namespace pocket {

std::string companion_pair_url(std::string_view code) {
  std::string url = POCKET_PWA_ORIGIN;
  url += "/link?code=";
  url.append(code.data(), code.size());
  return url;
}

std::string companion_link_url() {
  std::string url = POCKET_PWA_ORIGIN;
  url += "/link";
  return url;
}

std::string softap_portal_url() {
  return "http://192.168.4.1/";
}

std::string companion_wifi_setup_url() {
  // Legacy alias — Wi‑Fi is part of Link now.
  return companion_link_url();
}

std::string companion_download_url() {
  return POCKET_PWA_ORIGIN;
}

/** Human-readable site line for e-ink (no scheme). */
std::string companion_display_origin() {
  std::string_view o = POCKET_PWA_ORIGIN;
  if (o.size() > 8 && o.substr(0, 8) == "https://") o.remove_prefix(8);
  else if (o.size() > 7 && o.substr(0, 7) == "http://") o.remove_prefix(7);
  return std::string(o);
}

std::string generate_pair_code() {
  static std::mt19937 rng{static_cast<unsigned>(std::time(nullptr))};
  std::uniform_int_distribution<int> dist(0, 31);  // alphabet length 32
  std::string code;
  code.reserve(8);
  for (int i = 0; i < 8; ++i) {
    code.push_back(kPairCodeAlphabet[dist(rng) % 32]);
  }
  return code;
}

}  // namespace pocket
