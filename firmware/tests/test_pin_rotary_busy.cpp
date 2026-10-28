// Multi-tick PIN rotary must count every detent even while a partial refresh is
// "busy". Repro: Down, Down for digit 2 — if flush blocks on the first present,
// the second tick is lost and the wheel only shows 1.
#include "pocket/app.hpp"
#include "pocket/input.hpp"
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
  int battery_percent() override { return 80; }
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

/** Display that stays busy for N pump iterations after each present.
 * Skips when busy — matches device EpdDisplay (present must not clear dirty). */
struct BusyDisp : PlatformDisplay {
  int region = 0;
  int full_or_fast = 0;
  int busy_left = 0;
  bool busy() override { return busy_left > 0; }
  void advance() {
    if (busy_left > 0) --busy_left;
  }
  void present(const Canvas&, RefreshMode) override {
    if (busy_left > 0) return;
    ++full_or_fast;
    busy_left = 4;
  }
  void present_region(const Canvas&, int, int, int, int) override {
    if (busy_left > 0) return;
    ++region;
    busy_left = 4;
  }
};

/** Mirrors app_main: apply rotary immediately; flush once per pump (busy-aware). */
static void pump(InputMapper& mapper, App& app, BusyDisp& disp) {
  disp.advance();
  for (;;) {
    const InputEvent e = mapper.poll();
    if (e == InputEvent::None) break;
    if (e == InputEvent::Up || e == InputEvent::Down) {
      int delta = (e == InputEvent::Down) ? 1 : -1;
      delta += mapper.drain_up_down_net();
      app.apply_coalesced_rotary(delta);
      continue;
    }
    app.flush_dirty();
    app.handle(e);
  }
  app.flush_dirty();
  app.tick(0);
}

int main() {
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "1234");
  store.mut().device_id = "00000000-0000-4000-8000-000000000090";

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  BusyDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  CHECK(app.screen() == ScreenId::Lock);

  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Pin);
  CHECK(app.pin_working_digit() == '0');

  InputMapper mapper;
  uint32_t t = 20000;

  // Pin enter kicks a full/partial — wait until the panel is idle before spinning.
  for (int i = 0; i < 16; ++i) {
    pump(mapper, app, disp);
    if (!disp.busy() && !app.needs_redraw()) break;
  }
  CHECK(!disp.busy());

  // First Down while idle → digit 1, one region present, panel now busy.
  mapper.on_button_down(true, t);
  const int region_before = disp.region;
  pump(mapper, app, disp);
  CHECK(app.pin_working_digit() == '1');
  CHECK(disp.region == region_before + 1);
  CHECK(disp.busy());
  CHECK(app.needs_redraw() == false);  // first present cleared dirty

  // Second Down while refresh is still busy — must advance to 2 (never drop).
  t += 40;
  mapper.on_button_down(true, t);
  const int region_mid = disp.region;
  pump(mapper, app, disp);
  CHECK(app.pin_working_digit() == '2');
  CHECK(disp.region == region_mid);   // no second present while busy
  CHECK(app.needs_redraw());          // deferred dirty kept

  // Third Down still busy → 3.
  t += 40;
  mapper.on_button_down(true, t);
  pump(mapper, app, disp);
  CHECK(app.pin_working_digit() == '3');
  CHECK(app.needs_redraw());

  // Drain busy window until the deferred digit-3 present has landed and settled.
  const int region_before_deferred = disp.region;
  bool saw_deferred = false;
  for (int i = 0; i < 24; ++i) {
    pump(mapper, app, disp);
    if (disp.region > region_before_deferred) saw_deferred = true;
    if (saw_deferred && !disp.busy() && !app.needs_redraw()) break;
  }
  CHECK(saw_deferred);
  CHECK(app.pin_working_digit() == '3');
  CHECK(disp.region >= region_before_deferred + 1);
  CHECK(!app.needs_redraw());

  // Classic repro: exactly two Downs for digit 2, second during busy.
  {
    MemoryConfigStore store2;
    store2.mut().onboarding_complete = true;
    store2.mut().pin_length = 4;
    set_pin(store2.mut(), "1234");
    store2.mut().device_id = "00000000-0000-4000-8000-000000000091";
    TClock clock2;
    TWifi wifi2;
    TCloud cloud2;
    BusyDisp disp2;
    App app2(store2, clock2, wifi2, cloud2, disp2);
    app2.boot();
    app2.handle(InputEvent::Select);
    InputMapper m2;
    uint32_t t2 = 30000;
    for (int i = 0; i < 16; ++i) {
      pump(m2, app2, disp2);
      if (!disp2.busy() && !app2.needs_redraw()) break;
    }
    m2.on_button_down(true, t2);
    pump(m2, app2, disp2);
    CHECK(app2.pin_working_digit() == '1');
    CHECK(disp2.busy());
    m2.on_button_down(true, t2 + 40);
    pump(m2, app2, disp2);
    CHECK(app2.pin_working_digit() == '2');  // both clicks counted
    for (int i = 0; i < 24; ++i) {
      pump(m2, app2, disp2);
      if (!disp2.busy() && !app2.needs_redraw()) break;
    }
    CHECK(app2.pin_working_digit() == '2');
  }

  // Mapper overflow must not drop rotary: fill queue, then more Downs.
  {
    InputMapper m3;
    uint32_t t3 = 40000;
    // Push more than kQueue Downs spaced past debounce.
    for (int i = 0; i < 40; ++i) {
      m3.on_button_down(true, t3 + static_cast<uint32_t>(i) * 40u);
    }
    InputEvent ev = m3.poll();
    CHECK(ev == InputEvent::Down);
    int delta = 1 + m3.drain_up_down_net();
    CHECK(delta == 40);  // every detent counted
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_pin_rotary_busy OK");
  return 0;
}
