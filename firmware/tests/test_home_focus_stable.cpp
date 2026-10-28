/** Home rotary must move focus only — never invent/duplicate apps or rearrange. */
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
  uint32_t now_ms() override { return t; }
  void local_hm(int& h, int& m, int& weekday, int& month, int& day) override {
    h = 10;
    m = 5;
    weekday = 1;
    month = 3;
    day = 24;
  }
};
struct TWifi : PlatformWifi {
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return true; }
  bool connected() const override { return true; }
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
  set_pin(store.mut(), "0000");
  store.mut().home_visible = 0x01FF;
  store.mut().device_id = "00000000-0000-4000-8000-000000000077";

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);

  // One Down: focus advances by exactly 1; one content-band present; no full LUT.
  const int r0 = disp.region;
  const int f0 = disp.full_or_fast;
  app.apply_home_focus_delta(1);
  app.flush_dirty();
  CHECK(disp.region == r0 + 1);
  CHECK(disp.full_or_fast == f0);
  CHECK(disp.last_ry == kStatusBarH);
  CHECK(disp.last_rh == kCanvasH - kStatusBarH);

  // Twenty more single steps — content-band half-credit toward ghosting must not
  // force a Full (0xD7) mid-browse before ~32 focus moves (v97).
  for (int i = 0; i < 20; ++i) {
    const int r = disp.region;
    const int f = disp.full_or_fast;
    app.apply_home_focus_delta(1);
    app.flush_dirty();
    CHECK(disp.region == r + 1);
    CHECK(disp.full_or_fast == f);
    CHECK(disp.last_ry == kStatusBarH);
  }

  // Coalesce burst: net +2 → one present, content band.
  const int rc = disp.region;
  const int fc = disp.full_or_fast;
  app.apply_coalesced_rotary(2);
  app.flush_dirty();
  CHECK(disp.region == rc + 1);
  CHECK(disp.full_or_fast == fc);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_home_focus_stable OK");
  return 0;
}
