#include "pocket/app.hpp"
#include <cstdio>
#include <cstdlib>
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
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 10;
    minute = 5;
    weekday = 1;
    month = 3;
    day = 24;
  }
};
struct TWifi : PlatformWifi {
  bool ok = true;
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return true; }
  bool connected() const override { return ok; }
  bool start_provision(const std::string&, std::string* ap_ssid_out, std::string* ap_pass_out) override {
    if (ap_ssid_out) *ap_ssid_out = "Pocket-TEST";
    if (ap_pass_out) *ap_pass_out = "AB23CD45";
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
  std::string music_list_json(const std::string&) override {
    return R"([{"id":"t1","title":"Chime","filename":"chime.wav","size":44}])";
  }
  std::vector<uint8_t> music_download(const std::string&, const std::string&) override {
    static const uint8_t wav[] = {
        'R', 'I', 'F', 'F', 36, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0,
        1, 0, 1, 0, 0x44, 0xac, 0, 0, 0x88, 0x58, 0x01, 0, 2, 0, 16, 0, 'd', 'a', 't', 'a', 0, 0, 0, 0};
    return std::vector<uint8_t>(wav, wav + sizeof(wav));
  }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
};
struct TStorage : PlatformStorage {
  std::string root = "/tmp/pocket-music-test";
  std::string music_root() override { return root; }
  bool music_ensure_root() override {
    std::string cmd = "mkdir -p " + root;
    return std::system(cmd.c_str()) == 0;
  }
  uint64_t free_bytes(const std::string&) override { return 8ull * 1024 * 1024; }
};
struct TAudio : PlatformAudio {
  std::string last_path;
  int plays = 0;
  bool play_file(const std::string& path) override {
    last_path = path;
    ++plays;
    return !path.empty();
  }
  void stop() override {}
};

int main() {
  MemoryConfigStore store;
  auto& cfg = store.mut();
  cfg.onboarding_complete = true;
  cfg.device_id = "dev-test";
  cfg.home_visible = 0x007F;
  cfg.pin_length = 4;
  set_pin(cfg, "0000");

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  TStorage storage;
  TAudio audio;
  App app(store, clock, wifi, cloud, disp, &storage, &audio);
  app.boot();

  // Lock → PIN → Home (Select opens PIN; four more set 0000)
  CHECK(app.screen() == ScreenId::Lock);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Pin);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);

  // Focus: Notes(0) Ledger(1) Clock(2) Pass(3) Weather(4) Music(5)
  for (int i = 0; i < 5; ++i) app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::MusicList);

  // Empty library → Sync at index 0
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::MusicList);

  // After sync, index 0 is the track
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::MusicNowPlaying);
  CHECK(audio.plays >= 1);
  CHECK(audio.last_path.find("chime.wav") != std::string::npos);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_home_music OK");
  return 0;
}
