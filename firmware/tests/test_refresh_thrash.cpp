// Regression coverage for "button/rotary presses thrash the e-ink display":
// a single physical press (with mechanical switch bounce) must drive at most
// one partial refresh, and the ghosting-mitigation full refresh must fire
// exactly once — never stack into a cascade of full refreshes.
#include "pocket/app.hpp"
#include "pocket/input.hpp"
#include "pocket/refresh.hpp"
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
  void present(const Canvas&, RefreshMode) override { ++full_or_fast; }
  void present_region(const Canvas&, int, int, int, int) override { ++region; }
};

/** Mirrors the app_main input loop: coalesce Up/Down, one present per pump. */
static void drain(InputMapper& mapper, App& app) {
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
}

int main() {
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "0000");
  store.mut().device_id = "00000000-0000-4000-8000-000000000099";

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  CHECK(app.screen() == ScreenId::Lock);

  // Unlock onto Home (PIN digit stays '0' by default — matches set_pin("0000")).
  app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);

  // --- Bounce on a single physical Up press must yield exactly one refresh ---
  InputMapper mapper;
  uint32_t t = 10000;
  mapper.on_button_up(true, t);
  mapper.on_button_up(false, t + 4);
  mapper.on_button_up(true, t + 9);
  mapper.on_button_up(false, t + 15);
  mapper.on_button_up(true, t + 22);

  const int region_before = disp.region;
  const int full_before = disp.full_or_fast;
  drain(mapper, app);
  CHECK(disp.region == region_before + 1);   // exactly one partial for the whole bounce burst
  CHECK(disp.full_or_fast == full_before);   // bounce must never sneak in a full refresh

  // Ghosting budget: well-spaced partials, then exactly ONE full — not a cascade.
  // Unlock/Home enter now use 0xFF windows (they count as partials), so drive until
  // the next full rather than assuming a zeroed counter.
  int safety = 0;
  while (disp.full_or_fast == full_before && safety < RefreshPolicy::kGhostingN + 8) {
    t += 500;
    mapper.on_button_up(true, t);
    mapper.on_button_up(false, t + 50);
    drain(mapper, app);
    ++safety;
  }
  CHECK(disp.full_or_fast == full_before + 1);

  t += 500;
  mapper.on_button_up(true, t);
  mapper.on_button_up(false, t + 50);
  const int full_after_trip = disp.full_or_fast;
  drain(mapper, app);
  CHECK(disp.full_or_fast == full_after_trip);  // no extra full — counter reset, back to partial

  // --- Notes list: coalesced rotary burst → exactly one region present ---
  // Fresh app so Home focus is Notes (index 0); prior Up thrash moved focus away.
  {
    MemoryConfigStore store2;
    store2.mut().onboarding_complete = true;
    store2.mut().pin_length = 4;
    set_pin(store2.mut(), "0000");
    store2.mut().device_id = "00000000-0000-4000-8000-000000000098";
    TClock clock2;
    TWifi wifi2;
    TCloud cloud2;
    TDisp disp2;
    App notes_app(store2, clock2, wifi2, cloud2, disp2);
    notes_app.boot();
    notes_app.handle(InputEvent::Select);  // Lock → Pin
    for (int i = 0; i < 4; ++i) notes_app.handle(InputEvent::Select);
    CHECK(notes_app.screen() == ScreenId::Home);
    notes_app.handle(InputEvent::Select);  // Notes
    CHECK(notes_app.screen() == ScreenId::NotesList);
    // Seed a few notes so list↔list focus uses region dirty (not FAB content band).
    for (int i = 0; i < 3; ++i) {
      notes_app.handle(InputEvent::Select);  // New note → detail
      CHECK(notes_app.screen() == ScreenId::NotesDetail);
      notes_app.handle(InputEvent::Back);
      CHECK(notes_app.screen() == ScreenId::NotesList);
    }
    const int notes_region_before = disp2.region;
    const int notes_full_before = disp2.full_or_fast;
    InputMapper mapper2;
    uint32_t t2 = 50000;
    mapper2.on_button_down(true, t2);
    mapper2.on_button_down(true, t2 + 200);
    mapper2.on_button_down(true, t2 + 400);
    drain(mapper2, notes_app);
    CHECK(disp2.region == notes_region_before + 1);
    CHECK(disp2.full_or_fast == notes_full_before);
    CHECK(notes_app.screen() == ScreenId::NotesList);

    // Further rotary still partial-only (no full-panel LUT yank).
    const int wrap_region = disp2.region;
    const int wrap_full = disp2.full_or_fast;
    notes_app.apply_coalesced_rotary(1);
    notes_app.flush_dirty();
    CHECK(disp2.region == wrap_region + 1);
    CHECK(disp2.full_or_fast == wrap_full);
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_refresh_thrash OK");
  return 0;
}
