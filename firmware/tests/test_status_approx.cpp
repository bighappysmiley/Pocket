/** Status bar must refresh when wall-clock quality flips Approximate ↔ Trusted. */
#include "pocket/app.hpp"
#include <cstdio>
#include <string>
#include <vector>

using namespace pocket;

static int failures = 0;
#define CHECK(cond) \
  do { \
    if (!(cond)) { \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
      ++failures; \
    } \
  } while (0)

struct QClock : PlatformClock {
  uint32_t t = 0;
  bool valid = true;
  bool needs = true;  // Approximate
  uint32_t now_ms() override { return t; }
  void local_hm(int& h, int& m, int& weekday, int& month, int& day) override {
    h = 10;
    m = 5;
    weekday = 1;
    month = 3;
    day = 24;
  }
  bool time_valid() const override { return valid; }
  bool time_trusted() const override { return valid && !needs; }
  bool time_needs_network_sync() const override { return needs; }
  int battery_percent() override { return 80; }
};
struct TWifi : PlatformWifi {
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return true; }
  bool connected() const override { return false; }
  bool start_provision(const std::string&, std::string* ap, std::string* pw) override {
    if (ap) *ap = "Pocket-TEST";
    if (pw) *pw = "AB23CD45";
    return true;
  }
  void stop_provision() override {}
  bool take_provision_credentials(std::string*, std::string*) override { return false; }
  std::string provision_ap_ssid() const override { return "Pocket-TEST"; }
  std::string provision_ap_password() const override { return "AB23CD45"; }
};
struct TCloud : PlatformCloud {
  std::string create_pair_session(const std::string&) override { return {}; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
};
struct TDisp : PlatformDisplay {
  int region = 0;
  int status_band = 0;
  void present(const Canvas&, RefreshMode) override {}
  void present_region(const Canvas&, int /*x*/, int y, int /*w*/, int h) override {
    ++region;
    if (y == 0 && h <= kStatusBarH + 4) ++status_band;
  }
};

int main() {
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "0000");
  store.mut().device_id = "00000000-0000-4000-8000-000000000095";

  QClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);

  // Prime status baseline (Approximate).
  clock.t = 1000;
  app.tick(clock.t);
  const int status0 = disp.status_band;

  // Flip to Trusted — status chrome must dirty + present status band.
  clock.needs = false;
  clock.t = 1100;
  app.tick(clock.t);
  CHECK(disp.status_band > status0);

  // Flip back to Approximate.
  const int status1 = disp.status_band;
  clock.needs = true;
  clock.t = 1200;
  app.tick(clock.t);
  CHECK(disp.status_band > status1);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_status_approx OK");
  return 0;
}
