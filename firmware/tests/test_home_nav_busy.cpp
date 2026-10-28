/** Home / Settings rotary + Select must keep dirty while panel busy (v92 contract). */
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

static void pump(InputMapper& mapper, App& app, BusyDisp& disp, uint32_t now_ms = 0) {
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
  app.tick(now_ms);
}

static void settle(InputMapper& mapper, App& app, BusyDisp& disp) {
  for (int i = 0; i < 24; ++i) {
    pump(mapper, app, disp);
    if (!disp.busy() && !app.needs_redraw()) break;
  }
}

static void unlock_home(App& app, InputMapper& mapper, BusyDisp& disp) {
  app.boot();
  settle(mapper, app, disp);
  app.handle(InputEvent::Select);  // Lock → Pin
  settle(mapper, app, disp);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);  // PIN 0000
  settle(mapper, app, disp);
  CHECK(app.screen() == ScreenId::Home);
}

int main() {
  // --- Home focus while busy: net delta kept, deferred present ---
  {
    MemoryConfigStore store;
    store.mut().onboarding_complete = true;
    store.mut().pin_length = 4;
    set_pin(store.mut(), "0000");
    store.mut().home_visible = 0x01FF;
    store.mut().device_id = "00000000-0000-4000-8000-000000000094";
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app(store, clock, wifi, cloud, disp);
    InputMapper mapper;
    unlock_home(app, mapper, disp);

    const int focus0 = app.home_focus_index();
    // Kick a present so panel is busy, then spin while busy.
    app.apply_home_focus_delta(1);
    app.flush_dirty();
    CHECK(disp.busy());
    CHECK(app.home_focus_index() == focus0 + 1);

    // Second + third ticks while busy — must advance, keep dirty, no second present.
    const int region_mid = disp.region;
    app.apply_coalesced_rotary(2);
    app.flush_dirty();
    CHECK(app.home_focus_index() == focus0 + 3);
    CHECK(disp.region == region_mid);
    CHECK(app.needs_redraw());

    settle(mapper, app, disp);
    CHECK(disp.region > region_mid);
    CHECK(!app.needs_redraw());
    CHECK(app.home_focus_index() == focus0 + 3);
  }

  // --- Select while busy launches app; deferred paint keeps dirty ---
  {
    MemoryConfigStore store;
    store.mut().onboarding_complete = true;
    store.mut().pin_length = 4;
    set_pin(store.mut(), "0000");
    store.mut().home_visible = 0x01FF;
    store.mut().device_id = "00000000-0000-4000-8000-000000000095";
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app(store, clock, wifi, cloud, disp);
    InputMapper mapper;
    unlock_home(app, mapper, disp);

    // Focus Notes (index 0 typically) — ensure we're on a known app.
    while (app.home_focus_index() > 0) app.apply_home_focus_delta(-1);
    settle(mapper, app, disp);

    // Move focus once to kick busy, then Select Notes from focus 0 after re-zero.
    while (app.home_focus_index() > 0) app.apply_home_focus_delta(-1);
    app.apply_home_focus_delta(1);
    app.flush_dirty();
    CHECK(disp.busy());
    // Back to Notes while still busy
    app.apply_home_focus_delta(-1);
    CHECK(app.needs_redraw());
    app.handle(InputEvent::Select);
    CHECK(app.screen() == ScreenId::NotesList);
    CHECK(app.needs_redraw());  // after_nav must keep dirty if present skipped

    const int before = disp.full_or_fast + disp.region;
    bool painted = false;
    for (int i = 0; i < 32; ++i) {
      pump(mapper, app, disp);
      if ((disp.full_or_fast + disp.region) > before) painted = true;
      if (painted && !disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(painted);
    CHECK(app.screen() == ScreenId::NotesList);
    CHECK(!app.needs_redraw());
  }

  // --- Settings list coalesce under busy ---
  {
    MemoryConfigStore store;
    store.mut().onboarding_complete = true;
    store.mut().pin_length = 4;
    set_pin(store.mut(), "0000");
    store.mut().home_visible = 0x01FF;
    store.mut().device_id = "00000000-0000-4000-8000-000000000096";
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app(store, clock, wifi, cloud, disp);
    InputMapper mapper;
    unlock_home(app, mapper, disp);

    // Display order: Notes…Music, Reading, Settings, Update — Settings at index 7.
    while (app.home_focus_index() > 0) app.apply_home_focus_delta(-1);
    settle(mapper, app, disp);
    app.apply_home_focus_delta(7);
    settle(mapper, app, disp);
    app.handle(InputEvent::Select);
    settle(mapper, app, disp);
    CHECK(app.screen() == ScreenId::SettingsRoot);

    // Burst Down while busy — one deferred present after idle.
    app.apply_coalesced_rotary(1);
    app.flush_dirty();
    CHECK(disp.busy());
    const int region_mid = disp.region;
    app.apply_coalesced_rotary(3);
    app.flush_dirty();
    CHECK(disp.region == region_mid);
    CHECK(app.needs_redraw());
    settle(mapper, app, disp);
    CHECK(disp.region > region_mid);
    CHECK(!app.needs_redraw());
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_home_nav_busy OK");
  return 0;
}
