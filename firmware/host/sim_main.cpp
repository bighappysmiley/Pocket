#include "pocket/app.hpp"
#include "pocket/input.hpp"
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace pocket;

struct HostClock : PlatformClock {
  uint32_t t0 =
      static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count());
  uint32_t now_ms() override {
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count()) -
           t0;
  }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    std::time_t t = std::time(nullptr);
    std::tm* tm = std::localtime(&t);
    hour = tm->tm_hour;
    minute = tm->tm_min;
    weekday = tm->tm_wday;
    month = tm->tm_mon;
    day = tm->tm_mday;
  }
};

struct HostWifi : PlatformWifi {
  bool ok = false;
  std::vector<std::string> scan() override { return {"HomeNet", "CafeWiFi", "PocketLab"}; }
  bool connect(const std::string& /*ssid*/, const std::string& password) override {
    ok = password.size() >= 1;
    return ok;
  }
  bool connected() const override { return ok; }
};

struct HostCloud : PlatformCloud {
  std::string code = "ABCD2345";
  std::string status = "pending";
  std::string create_pair_session(const std::string& /*device_id*/) override {
    code = "ABCD2345";
    status = "pending";
    return code;
  }
  std::string pair_status(const std::string& /*c*/) override { return status; }
  void refresh_entitlement(DeviceConfig& cfg) override {
    cfg.cloud_entitled = false;
    cfg.cloud_status = "free";
  }
  std::string stt_transcribe(const std::vector<uint8_t>& /*pcm*/) override {
    return "hello pocket";
  }
};

struct HostDisplay : PlatformDisplay {
  void present(const Canvas& canvas, RefreshMode mode) override {
    // ASCII preview: downsample 480x800 → 48x40 blocks
    std::printf("\033[2J\033[H");
    std::printf("Pocket host sim · refresh=%s · 480x800\n",
                mode == RefreshMode::Full ? "FULL" : "partial");
    const char* shades = " .:-=+*#%@";
    for (int y = 0; y < 40; ++y) {
      for (int x = 0; x < 48; ++x) {
        int sx = x * kCanvasW / 48;
        int sy = y * kCanvasH / 40;
        Gray g = canvas.get_pixel(sx, sy);
        int idx = (3 - static_cast<int>(g)) * 2;
        std::putchar(shades[idx]);
      }
      std::putchar('\n');
    }
    std::fflush(stdout);
  }
};

static void help() {
  std::puts("Keys: j/k Down/Up | Enter Select | H Home | b Back | B PTT | p Power | c claim-pair | q quit");
}

int main() {
  MemoryConfigStore store;
  HostClock clock;
  HostWifi wifi;
  HostCloud cloud;
  HostDisplay display;
  App app(store, clock, wifi, cloud, display);
  InputMapper mapper;
  app.boot();
  help();

  // Non-blocking-ish stdin: line mode commands for portability
  std::string line;
  while (true) {
    std::printf("> ");
    if (!std::getline(std::cin, line)) break;
    if (line.empty()) continue;
    char c = line[0];
    uint32_t now = clock.now_ms();
    if (c == 'q') break;
    if (c == '?') {
      help();
      continue;
    }
    if (c == 'c') {
      cloud.status = "claimed";
      continue;
    }
    switch (c) {
      case 'k':
        mapper.on_button_up(true, now);
        break;
      case 'j':
        mapper.on_button_down(true, now);
        break;
      case '\n':
      case ' ':
      case 'e':
        mapper.on_button_function(true, now);
        mapper.on_button_function(false, now + 50);
        break;
      case 'H':
        mapper.on_button_function(true, now);
        mapper.tick(now + 850);
        mapper.on_button_function(false, now + 900);
        break;
      case 'b':
        mapper.on_boot(true, now);
        mapper.on_boot(false, now + 50);
        break;
      case 'B':
        mapper.on_boot(true, now);
        mapper.tick(now + 250);
        mapper.on_boot(false, now + 800);
        break;
      case 'p':
        mapper.on_pwr(true, now);
        mapper.on_pwr(false, now + 50);
        break;
      default:
        std::puts("unknown key");
        continue;
    }
    InputEvent ev;
    while ((ev = mapper.poll()) != InputEvent::None) {
      app.handle(ev);
    }
    app.tick(clock.now_ms());
  }
  return 0;
}