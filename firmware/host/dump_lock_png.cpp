/** Render each lock motif to a grayscale PPM for QA. */
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

static bool write_ppm(const char* path, const Canvas& c) {
  FILE* f = std::fopen(path, "wb");
  if (!f) {
    std::perror(path);
    return false;
  }
  std::fprintf(f, "P6\n%d %d\n255\n", kCanvasW, kCanvasH);
  for (int y = 0; y < kCanvasH; ++y) {
    for (int x = 0; x < kCanvasW; ++x) {
      const Gray g = c.get_pixel(x, y);
      const uint8_t v = static_cast<uint8_t>(static_cast<int>(g) * 85);
      std::fputc(v, f);
      std::fputc(v, f);
      std::fputc(v, f);
    }
  }
  std::fclose(f);
  return true;
}

int main(int argc, char** argv) {
  const char* out_dir = (argc > 1) ? argv[1] : ".";
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "0000");
  store.mut().device_name = "Alex";
  store.mut().lock_message = "Alex";
  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  if (app.screen() != ScreenId::Lock) {
    std::fprintf(stderr, "expected Lock, got %d\n", static_cast<int>(app.screen()));
    return 1;
  }

  static const char* kNames[] = {"horizon", "tide", "ridge", "constellation", "harbor", "aperture"};
  for (int i = 0; i < kLockMotifCount; ++i) {
    if (i > 0) app.handle(InputEvent::Power);  // go_lock() — advances motif
    if (app.lock_motif_index() != i) {
      std::fprintf(stderr, "motif index %d expected %d\n", app.lock_motif_index(), i);
      return 1;
    }
    app.redraw(true);
    char path[512];
    std::snprintf(path, sizeof(path), "%s/%s.ppm", out_dir, kNames[i]);
    if (!write_ppm(path, app.canvas())) return 1;
    std::printf("wrote %s (motif %d)\n", path, i);
  }
  return 0;
}
