// Regression coverage for "text stays one line / clipped": NotesDetail used to
// draw a dictated note's body with a single unwrapped draw_text() call, so any
// body longer than one line ran off the right edge of the screen instead of
// wrapping. This drives a real dictation round-trip and asserts the body paints
// ink across several distinct row-bands (i.e. it actually wrapped to multiple
// lines) instead of only the first.
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
  uint32_t now_ms() override { return 1000; }
  void local_hm(int& h, int& m, int& wd, int& mo, int& d) override {
    h = 10;
    m = 5;
    wd = 1;
    mo = 3;
    d = 24;
  }
};
struct TWifi : PlatformWifi {
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return true; }
  bool connected() const override { return true; }
  bool start_provision(const std::string&, std::string*, std::string*) override { return false; }
  void stop_provision() override {}
  bool take_provision_credentials(std::string*, std::string*) override { return false; }
  std::string provision_ap_ssid() const override { return {}; }
  std::string provision_ap_password() const override { return {}; }
};
struct TCloud : PlatformCloud {
  std::string create_pair_session(const std::string&) override { return {}; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override {
    return "This is a long dictated note body meant to prove word-wrap works "
           "correctly across many lines instead of clipping to a single line "
           "that runs off the right edge of the screen.";
  }
};
struct TAudio : PlatformAudio {
  bool mic_supported() override { return true; }
  MicCaptureResult stop_capture() override {
    MicCaptureResult r;
    r.ok = true;
    r.level_percent = 50;
    r.pcm = {1, 2, 3, 4};
    return r;
  }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
  void present_region(const Canvas&, int, int, int, int) override {}
};

/** True if any non-white pixel exists in [y0,y1) across the content width. */
static bool row_band_has_ink(const Canvas& c, int y0, int y1) {
  for (int y = y0; y < y1; ++y) {
    for (int x = kSideMargin; x < kCanvasW - kSideMargin; ++x) {
      if (c.get_pixel(x, y) != Gray::G3) return true;
    }
  }
  return false;
}

int main() {
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "0000");
  store.mut().device_id = "00000000-0000-4000-8000-000000000098";

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  TAudio audio;
  App app(store, clock, wifi, cloud, disp, nullptr, &audio);
  app.boot();
  CHECK(app.screen() == ScreenId::Lock);

  app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);

  app.handle(InputEvent::Select);  // Notes (first Home tile)
  CHECK(app.screen() == ScreenId::NotesList);
  app.handle(InputEvent::Select);  // New note
  CHECK(app.screen() == ScreenId::NotesDetail);

  app.handle(InputEvent::PttStart);
  app.handle(InputEvent::PttStop);

  const Canvas& c = app.canvas();
  // Body starts at kContentTop+52; Body line height is text_height+line_gap. A
  // single unwrapped line would only paint the first band below the title —
  // the wrapped body must also reach well past it.
  const int body_top = below_title(kContentTop);
  CHECK(row_band_has_ink(c, body_top, body_top + 30));        // line 1
  CHECK(row_band_has_ink(c, body_top + 120, body_top + 150)); // a later wrapped line
  CHECK(row_band_has_ink(c, body_top + 200, body_top + 230)); // further still

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_notes_wrap OK");
  return 0;
}
