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

struct TClock : PlatformClock {
  uint32_t t = 0;
  int hour = 10;
  int minute = 5;
  int batt = 80;
  uint32_t now_ms() override { return t; }
  void local_hm(int& h, int& m, int& weekday, int& month, int& day) override {
    h = hour;
    m = minute;
    weekday = 1;
    month = 3;
    day = 24;
  }
  int battery_percent() override { return batt; }
};
struct TWifi : PlatformWifi {
  bool ok = true;
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return true; }
  bool connected() const override { return ok; }
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
  int full_or_fast = 0;
  int region = 0;
  int last_ry = -1;
  int last_rh = -1;
  void present(const Canvas&, RefreshMode) override { ++full_or_fast; }
  void present_region(const Canvas&, int /*x*/, int y, int /*w*/, int h) override {
    ++region;
    last_ry = y;
    last_rh = h;
  }
};

int main() {
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "1234");
  store.mut().device_id = "00000000-0000-4000-8000-000000000042";

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  CHECK(app.screen() == ScreenId::Lock);
  CHECK(app.config().lock_message.empty());  // default: no bottom message on lock face

  // Unlock → PIN screen (full enter once)
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Pin);
  const int baseline_full = disp.full_or_fast;
  const int baseline_region = disp.region;

  // Spinning a digit must use region partial — not a full-panel refresh.
  app.handle(InputEvent::Down);
  CHECK(disp.region > baseline_region);
  CHECK(disp.full_or_fast == baseline_full);
  CHECK(disp.last_ry >= 100);  // PIN band, not status-only

  // Prime status-bar chrome tracker, then change minute → status region only
  clock.t += 500;
  app.tick(clock.t);
  const int after_prime_region = disp.region;
  const int after_prime_full = disp.full_or_fast;
  clock.minute = 6;
  clock.t += 1000;
  app.tick(clock.t);
  CHECK(disp.region > after_prime_region);
  CHECK(disp.full_or_fast == after_prime_full);
  CHECK(disp.last_ry == 0);
  CHECK(disp.last_rh <= 64);

  // Battery change → status-bar region
  const int after_min_region = disp.region;
  clock.batt = 55;
  clock.t += 1000;
  app.tick(clock.t);
  CHECK(disp.region > after_min_region);
  CHECK(disp.full_or_fast == after_prime_full);

  // Lock face starts on the first of the rotating calm motifs (no clock hands).
  CHECK(app.lock_motif_index() == 0);
  CHECK(app.lock_motif_index() >= 0 && app.lock_motif_index() < kLockMotifCount);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_partial_refresh OK");
  return 0;
}
