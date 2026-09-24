#include "pocket/cloud_client.hpp"
#include <cctype>
#include <cstdio>
#include <ctime>
#include <random>
#include <string>

namespace pocket {

std::string companion_pair_url(std::string_view code) {
  std::string url = POCKET_PWA_ORIGIN;
  url += "/pair?code=";
  url.append(code.data(), code.size());
  return url;
}

std::string companion_wifi_setup_url() {
  std::string url = POCKET_PWA_ORIGIN;
  url += "/wifi-setup";
  return url;
}

std::string companion_download_url() {
  return POCKET_PWA_ORIGIN;
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
