/** Lock/PIN input must stay alive while (and after) an async e-ink refresh.
 *
 * Repro (v90/v91): boot Lock paint kicks 0xD7 → busy; Select → after_nav redraw
 * skipped present but cleared dirty → screen frozen on Lock, buttons look dead.
 * Also: busy() that never clears (never-saw-high / stuck-high) must not brick UI.
 */
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

/** Realistic panel: skip present while busy (matches device EpdDisplay). */
struct BusyDisp : PlatformDisplay {
  int region = 0;
  int full_or_fast = 0;
  int skipped = 0;
  int busy_left = 0;
  bool busy() override { return busy_left > 0; }
  void advance() {
    if (busy_left > 0) --busy_left;
  }
  void present(const Canvas&, RefreshMode) override {
    if (busy_left > 0) {
      ++skipped;
      return;
    }
    ++full_or_fast;
    busy_left = 4;
  }
  void present_region(const Canvas&, int, int, int, int) override {
    if (busy_left > 0) {
      ++skipped;
      return;
    }
    ++region;
    busy_left = 4;
  }
};

/** Mirrors app_main input pump. Pass `now_ms` so chrome timers don't reset to 0. */
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

static App make_app(MemoryConfigStore& store, TClock& clock, TWifi& wifi, TCloud& cloud,
                    BusyDisp& disp) {
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "1234");
  store.mut().device_id = "00000000-0000-4000-8000-000000000092";
  return App(store, clock, wifi, cloud, disp);
}

int main() {
  // --- P0: Select on Lock while boot refresh still busy ---
  {
    MemoryConfigStore store;
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app = make_app(store, clock, wifi, cloud, disp);
    app.boot();
    CHECK(app.screen() == ScreenId::Lock);
    CHECK(disp.busy());  // boot Lock paint in flight

    // Direct Select while busy — must enter PIN and KEEP dirty for deferred paint.
    app.handle(InputEvent::Select);
    CHECK(app.screen() == ScreenId::Pin);
    CHECK(app.needs_redraw());  // must NOT have cleared dirty on skipped present
    CHECK(disp.busy());

    InputMapper mapper;
    // Rotary while still busy — digit advances in logical state.
    uint32_t t = 2000;
    mapper.on_button_down(true, t);
    pump(mapper, app, disp);
    CHECK(app.pin_working_digit() == '1');
    CHECK(app.needs_redraw());

    t += 40;
    mapper.on_button_down(true, t);
    pump(mapper, app, disp);
    CHECK(app.pin_working_digit() == '2');

    // Drain busy until deferred PIN paint lands.
    const int presents_before = disp.full_or_fast + disp.region;
    bool painted = false;
    for (int i = 0; i < 32; ++i) {
      pump(mapper, app, disp);
      if ((disp.full_or_fast + disp.region) > presents_before) painted = true;
      if (painted && !disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(painted);
    CHECK(app.screen() == ScreenId::Pin);
    CHECK(app.pin_working_digit() == '2');
    CHECK(!app.needs_redraw());
  }

  // --- Select via mapper while busy (full app_main path; Fn releases → Select) ---
  {
    MemoryConfigStore store;
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app = make_app(store, clock, wifi, cloud, disp);
    app.boot();
    CHECK(disp.busy());

    InputMapper mapper;
    uint32_t t = 5000;
    mapper.on_button_function(true, t);
    mapper.on_button_function(false, t + 50);  // release → Select
    pump(mapper, app, disp);
    CHECK(app.screen() == ScreenId::Pin);
    CHECK(app.needs_redraw() || !disp.busy());  // dirty kept if still busy

    for (int i = 0; i < 24; ++i) {
      pump(mapper, app, disp);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(app.screen() == ScreenId::Pin);
    CHECK(!app.needs_redraw());

    // After clock-init style settle: rotary + Select still work.
    t += 200;
    mapper.on_button_down(true, t);
    pump(mapper, app, disp);
    CHECK(app.pin_working_digit() == '1');
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    t += 200;
    mapper.on_button_function(true, t);
    mapper.on_button_function(false, t + 50);
    pump(mapper, app, disp);
    CHECK(app.pin_entry().size() == 1);
    CHECK(app.pin_entry()[0] == '1');
  }

  // --- Stuck busy that eventually advances: deferred Lock→PIN must still paint ---
  {
    MemoryConfigStore store;
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app = make_app(store, clock, wifi, cloud, disp);
    app.boot();
    // Hold busy artificially long (simulates never-saw-high until timeout clears).
    disp.busy_left = 12;
    app.handle(InputEvent::Select);
    CHECK(app.screen() == ScreenId::Pin);
    CHECK(app.needs_redraw());
    InputMapper mapper;
    const int before = disp.full_or_fast + disp.region;
    bool painted = false;
    for (int i = 0; i < 40; ++i) {
      pump(mapper, app, disp);
      if ((disp.full_or_fast + disp.region) > before) painted = true;
      if (painted && !app.needs_redraw()) break;
    }
    CHECK(painted);
    CHECK(app.screen() == ScreenId::Pin);
  }

  // --- PIN unlock → Home while busy (sister of Lock→PIN deferred paint) ---
  {
    MemoryConfigStore store;
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app = make_app(store, clock, wifi, cloud, disp);
    app.boot();
    InputMapper mapper;
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    app.handle(InputEvent::Select);  // Lock → Pin
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(app.screen() == ScreenId::Pin);

    // Enter 1234 — leave panel busy after the last digit present.
    const char* pin = "1234";
    for (int d = 0; d < 4; ++d) {
      while (app.pin_working_digit() != pin[d]) app.apply_pin_digit_delta(1);
      app.flush_dirty();
      app.handle(InputEvent::Select);
      clock.t += 200;
      // Advance busy but keep last commit's refresh inflight for the Home enter.
      if (d < 3) {
        for (int i = 0; i < 16; ++i) {
          pump(mapper, app, disp);
          if (!disp.busy() && !app.needs_redraw()) break;
        }
      }
    }
    CHECK(app.screen() == ScreenId::Home);
    // go_home → after_nav must keep dirty if present was skipped.
    CHECK(app.needs_redraw() || !disp.busy());
    const int before = disp.full_or_fast + disp.region;
    bool painted = false;
    for (int i = 0; i < 32; ++i) {
      pump(mapper, app, disp);
      if ((disp.full_or_fast + disp.region) > before) painted = true;
      if (painted && !disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(app.screen() == ScreenId::Home);
    CHECK(painted || before > 0);  // Home paint landed (or was already idle-painted)
    CHECK(!app.needs_redraw());
  }

  // --- Incorrect PIN chrome expires without full remount ---
  {
    MemoryConfigStore store;
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app = make_app(store, clock, wifi, cloud, disp);
    app.boot();
    InputMapper mapper;
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    app.handle(InputEvent::Select);
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    // Wrong PIN 0000
    for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
    CHECK(app.screen() == ScreenId::Pin);
    clock.t += 50;
    app.tick(clock.t);  // arm pin chrome while Incorrect PIN is live
    CHECK(app.pin_chrome_live());
    const int before = disp.region + disp.full_or_fast;
    // Expire Incorrect PIN toast (2s) — chrome wipe must dirty + present (region).
    clock.t += 2100;
    app.tick(clock.t);
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp, clock.t);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(!app.pin_chrome_live());
    CHECK(disp.region + disp.full_or_fast > before);
  }

  // --- Live lockout countdown ticks each second ---
  {
    MemoryConfigStore store;
    TClock clock;
    TWifi wifi;
    TCloud cloud;
    BusyDisp disp;
    App app = make_app(store, clock, wifi, cloud, disp);
    app.boot();
    InputMapper mapper;
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp, clock.t);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    app.handle(InputEvent::Select);
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp, clock.t);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    // Five wrong PINs → 30s lockout
    for (int attempt = 0; attempt < 5; ++attempt) {
      for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
      clock.t += 50;
      app.tick(clock.t);
      for (int i = 0; i < 8; ++i) {
        pump(mapper, app, disp, clock.t);
        if (!disp.busy() && !app.needs_redraw()) break;
      }
    }
    const uint32_t rem0 = app.pin_lockout_remaining_s();
    CHECK(rem0 >= 29 && rem0 <= 30);
    CHECK(app.pin_chrome_live());
    // Drain so countdown present from lockout-enter is idle before the 1 Hz tick.
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp, clock.t);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    const int before = disp.region + disp.full_or_fast;
    // Advance ~1.1s — remaining drops by one and chrome must re-present.
    clock.t += 1100;
    app.tick(clock.t);
    for (int i = 0; i < 16; ++i) {
      pump(mapper, app, disp, clock.t);
      if (!disp.busy() && !app.needs_redraw()) break;
    }
    CHECK(app.pin_lockout_remaining_s() == rem0 - 1);
    CHECK(disp.region + disp.full_or_fast > before);
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_lock_input_busy OK");
  return 0;
}
