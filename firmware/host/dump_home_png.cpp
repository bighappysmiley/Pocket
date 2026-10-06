/** Render Home grid to a grayscale PNG (via PPM + Pillow) for icon QA. */
#include "pocket/app.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace pocket;

struct TClock : PlatformClock {
  uint32_t now_ms() override { return 1000; }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 10;
    minute = 5;
    weekday = 1;
    month = 3;
    day = 24;
  }
  int battery_percent() override { return 86; }
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
};

int main(int argc, char** argv) {
  const char* out = (argc > 1) ? argv[1] : "home.ppm";
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "0000");
  store.mut().home_visible = 0x01FF;
  store.mut().device_id = "00000000-0000-4000-8000-000000000065";
  store.mut().device_name = "Pocket Classic";
  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  if (app.screen() != ScreenId::Home) {
    std::fprintf(stderr, "expected Home, got %d\n", static_cast<int>(app.screen()));
    return 1;
  }
  app.redraw(true);
  const Canvas& c = app.canvas();
  FILE* f = std::fopen(out, "wb");
  if (!f) {
    std::perror(out);
    return 1;
  }
  std::fprintf(f, "P6\n%d %d\n255\n", kCanvasW, kCanvasH);
  for (int y = 0; y < kCanvasH; ++y) {
    for (int x = 0; x < kCanvasW; ++x) {
      const Gray g = c.get_pixel(x, y);
      // G0 black … G3 white → 0 … 255
      const uint8_t v = static_cast<uint8_t>(static_cast<int>(g) * 85);
      std::fputc(v, f);
      std::fputc(v, f);
      std::fputc(v, f);
    }
  }
  std::fclose(f);
  std::printf("wrote %s (%dx%d)\n", out, kCanvasW, kCanvasH);
  return 0;
}
