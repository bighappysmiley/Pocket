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
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
  void present_region(const Canvas&, int, int, int, int) override {}
};

static App make_app(MemoryConfigStore& store, TClock& clock, TWifi& wifi, TCloud& cloud,
                    TDisp& disp) {
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "1234");
  store.mut().device_id = "00000000-0000-4000-8000-000000000064";
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  return app;
}

static void assert_pin_consistency(const App& app) {
  CHECK(app.pin_focus_index() == static_cast<int>(app.pin_entry().size()));
  CHECK(app.pin_working_digit() == app.pin_slot_value(app.pin_focus_index()));
  for (int i = 0; i < app.config().pin_length; ++i) {
    if (i < app.pin_focus_index()) {
      CHECK(app.pin_slot_value(i) == app.pin_entry()[static_cast<size_t>(i)]);
    } else if (i == app.pin_focus_index()) {
      CHECK(app.pin_slot_value(i) == app.pin_working_digit());
    } else {
      CHECK(app.pin_slot_value(i) == '0');
    }
  }
}

int main() {
  MemoryConfigStore store;
  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app = make_app(store, clock, wifi, cloud, disp);
  CHECK(app.screen() == ScreenId::Lock);

  // Enter PIN screen — focused slot is 0 showing '0'.
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Pin);
  CHECK(app.pin_focus_index() == 0);
  CHECK(app.pin_working_digit() == '0');
  assert_pin_consistency(app);

  // Spin digit 0 to 3 — displayed/working must track.
  app.apply_pin_digit_delta(3);
  app.flush_dirty();
  CHECK(app.pin_working_digit() == '3');
  CHECK(app.pin_slot_value(0) == '3');
  CHECK(app.pin_focus_index() == 0);
  assert_pin_consistency(app);

  // Coalesced Up/Down burst: net −1 from 3 → 2 (bounce cancels inside net).
  app.apply_pin_digit_delta(-2);
  app.apply_pin_digit_delta(1);  // simulate residual after coalesce to net -1 overall from 3→2
  // Reset cleanly: set to known value via absolute path (working is 2 now if -2+1 from 3).
  // Re-establish: clear by spinning to 4 for the classic glitch repro.
  while (app.pin_working_digit() != '4') {
    app.apply_pin_digit_delta(1);
  }
  app.flush_dirty();
  CHECK(app.pin_working_digit() == '4');
  CHECK(app.pin_focus_index() == 0);

  // Commit '4' → focus advances exactly one slot; new slot shows '0' (not stale 4).
  app.handle(InputEvent::Select);
  CHECK(app.pin_entry() == "4");
  CHECK(app.pin_focus_index() == 1);
  CHECK(app.pin_working_digit() == '0');
  CHECK(app.pin_slot_value(1) == '0');
  assert_pin_consistency(app);

  // Classic glitch: after advance, one Up must go 0→9, not jump to 3/4 from stale spin.
  app.handle(InputEvent::Up);
  CHECK(app.pin_focus_index() == 1);
  CHECK(app.pin_working_digit() == '9');
  assert_pin_consistency(app);

  // Bounce double-Select after a spun commit must NOT skip a slot.
  // Spin to 2, Select, immediate second Select (bounce) — focus advances by 1 only.
  app.apply_pin_digit_delta(3);  // 9→2 (wrap): +3 → 2
  // 9+3=12 → 2. Good.
  CHECK(app.pin_working_digit() == '2');
  app.handle(InputEvent::Select);
  CHECK(app.pin_entry() == "42");
  CHECK(app.pin_focus_index() == 2);
  CHECK(app.pin_working_digit() == '0');
  const int focus_after = app.pin_focus_index();
  app.handle(InputEvent::Select);  // bounce — must be ignored
  CHECK(app.pin_focus_index() == focus_after);
  CHECK(app.pin_entry() == "42");
  CHECK(app.pin_working_digit() == '0');
  assert_pin_consistency(app);

  // Moving focus via Back restores the prior index and shows '0' (slot unset again).
  app.handle(InputEvent::Back);
  CHECK(app.pin_entry() == "4");
  CHECK(app.pin_focus_index() == 1);
  CHECK(app.pin_working_digit() == '0');
  assert_pin_consistency(app);

  // Re-enter remaining digits 2,3,4 properly (PIN is 1234 — wrong so far; restart).
  app.handle(InputEvent::Back);
  CHECK(app.pin_entry().empty());
  CHECK(app.pin_focus_index() == 0);
  CHECK(app.pin_working_digit() == '0');

  // Full correct entry with coalesced spins between focus moves.
  for (int dig = 1; dig <= 4; ++dig) {
    CHECK(app.pin_focus_index() == dig - 1);
    CHECK(app.pin_working_digit() == '0');
    app.apply_pin_digit_delta(dig);
    app.flush_dirty();
    CHECK(app.pin_working_digit() == static_cast<char>('0' + dig));
    CHECK(app.pin_slot_value(dig - 1) == static_cast<char>('0' + dig));
    app.handle(InputEvent::Select);
  }
  CHECK(app.screen() == ScreenId::Home);

  // Mapper-level coalesce: Up/Down drain net applied once; focus never skips.
  {
    MemoryConfigStore store2;
    TClock clock2;
    TWifi wifi2;
    TCloud cloud2;
    TDisp disp2;
    App app2 = make_app(store2, clock2, wifi2, cloud2, disp2);
    app2.handle(InputEvent::Select);
    InputMapper mapper;
    uint32_t t = 20000;
    mapper.on_button_down(true, t);
    mapper.on_button_down(true, t + 200);
    mapper.on_button_down(true, t + 400);
    mapper.on_button_up(true, t + 600);
    InputEvent ev = mapper.poll();
    CHECK(ev == InputEvent::Down || ev == InputEvent::Up);
    int delta = (ev == InputEvent::Down) ? 1 : -1;
    delta += mapper.drain_up_down_net();
    app2.apply_pin_digit_delta(delta);
    app2.flush_dirty();
    // Net from Down,Down,Down,Up = +2 → working '2'
    CHECK(app2.pin_working_digit() == '2');
    CHECK(app2.pin_focus_index() == 0);
    assert_pin_consistency(app2);
    app2.handle(InputEvent::Select);
    CHECK(app2.pin_entry() == "2");
    CHECK(app2.pin_working_digit() == '0');
    CHECK(app2.pin_focus_index() == 1);
    assert_pin_consistency(app2);
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_pin_state OK");
  return 0;
}
