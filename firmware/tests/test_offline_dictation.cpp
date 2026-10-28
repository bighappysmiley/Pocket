/** Offline dictation: capture PCM while offline, queue locally, flush on Wi‑Fi. */
#include "pocket/app.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <vector>

using namespace pocket;

static int failures = 0;
#define CHECK(cond)                                                         \
  do {                                                                      \
    if (!(cond)) {                                                          \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);           \
      ++failures;                                                           \
    }                                                                       \
  } while (0)

struct TClock : PlatformClock {
  uint32_t t = 5000;
  uint32_t now_ms() override { return t; }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 9;
    minute = 41;
    weekday = 3;
    month = 9;
    day = 28;
  }
  bool time_valid() const override { return true; }
};
struct TWifi : PlatformWifi {
  bool on = false;
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override {
    on = true;
    return true;
  }
  bool connected() const override { return on; }
  bool start_provision(const std::string&, std::string* a, std::string* p) override {
    if (a) *a = "Pocket-TEST";
    if (p) *p = "AB23CD45";
    return true;
  }
  void stop_provision() override {}
  bool take_provision_credentials(std::string*, std::string*) override { return false; }
  std::string provision_ap_ssid() const override { return "Pocket-TEST"; }
  std::string provision_ap_password() const override { return "AB23CD45"; }
};
struct TCloud : PlatformCloud {
  int stt_calls = 0;
  std::string last_text = "hello from cloud";
  std::string create_pair_session(const std::string&) override { return {}; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>& pcm) override {
    ++stt_calls;
    if (pcm.empty()) return {};
    return last_text;
  }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
  void present_region(const Canvas&, int, int, int, int) override {}
};
struct TStorage : PlatformStorage {
  std::string root = "/tmp/pocket-offline-dictation-test";
  std::string data_root() override { return root; }
  bool data_ensure_root() override {
    ::mkdir(root.c_str(), 0755);
    ::mkdir((root + "/notes").c_str(), 0755);
    ::mkdir((root + "/lists").c_str(), 0755);
    return true;
  }
  std::string music_root() override { return root + "/music"; }
  bool music_ensure_root() override {
    data_ensure_root();
    ::mkdir((root + "/music").c_str(), 0755);
    return true;
  }
  std::string book_root() override { return root + "/books"; }
  bool book_ensure_root() override {
    data_ensure_root();
    ::mkdir((root + "/books").c_str(), 0755);
    return true;
  }
  uint64_t free_bytes(const std::string&) override { return 8ull * 1024 * 1024; }
};
struct TAudio : PlatformAudio {
  bool mic_supported() override { return true; }
  MicCaptureResult stop_capture() override {
    MicCaptureResult r;
    r.ok = true;
    r.level_percent = 40;
    r.pcm.assign(640, 0x11);  // fake PCM payload
    return r;
  }
};

static void unlock_home(App& app) {
  if (app.screen() == ScreenId::Lock) app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
}

int main() {
  std::system("rm -rf /tmp/pocket-offline-dictation-test");
  MemoryConfigStore store;
  auto& cfg = store.mut();
  cfg.onboarding_complete = true;
  cfg.pin_length = 4;
  set_pin(cfg, "0000");
  cfg.home_visible = 0x01FF;
  cfg.device_id = "offline-dictation";
  cfg.stt_path = 0;

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  TStorage storage;
  TAudio audio;
  App app(store, clock, wifi, cloud, disp, &storage, &audio);
  app.boot();
  unlock_home(app);
  CHECK(app.screen() == ScreenId::Home);

  app.handle(InputEvent::Select);  // Notes
  CHECK(app.screen() == ScreenId::NotesList);
  app.handle(InputEvent::Select);  // New note
  CHECK(app.screen() == ScreenId::NotesDetail);
  CHECK(cloud.stt_calls == 0);

  // Offline PTT — must queue, not call STT.
  CHECK(!wifi.connected());
  app.handle(InputEvent::PttStart);
  app.handle(InputEvent::PttStop);
  CHECK(cloud.stt_calls == 0);

  // Placeholder note persisted with pending PCM on disk.
  {
    std::ifstream in(storage.root + "/notes/index.json");
    CHECK(static_cast<bool>(in));
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(body.find(kVoicePendingText) != std::string::npos);
    CHECK(body.find("\"pending\"") != std::string::npos);
    CHECK(body.find("Voice note") != std::string::npos);
  }
  {
    // At least one .pcm under notes/pcm/
    std::ifstream idx(storage.root + "/notes/index.json");
    std::string body((std::istreambuf_iterator<char>(idx)), std::istreambuf_iterator<char>());
    const size_t p = body.find("\"pending\":\"");
    CHECK(p != std::string::npos);
    const size_t s = p + 11;
    const size_t e = body.find('"', s);
    CHECK(e != std::string::npos);
    const std::string name = body.substr(s, e - s);
    std::ifstream pcm(storage.root + "/notes/pcm/" + name, std::ios::binary);
    CHECK(static_cast<bool>(pcm));
    std::vector<char> buf((std::istreambuf_iterator<char>(pcm)), std::istreambuf_iterator<char>());
    CHECK(buf.size() == 640);
  }

  // Come online — flush replaces placeholder with transcript.
  wifi.on = true;
  app.notify_wifi_connected();
  CHECK(cloud.stt_calls == 1);
  {
    std::ifstream in(storage.root + "/notes/index.json");
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(body.find("hello from cloud") != std::string::npos);
    CHECK(body.find(kVoicePendingText) == std::string::npos);
    CHECK(body.find("\"pending\"") == std::string::npos);
  }

  // Online PTT still goes straight to STT.
  clock.t += 1000;
  app.handle(InputEvent::PttStart);
  app.handle(InputEvent::PttStop);
  CHECK(cloud.stt_calls == 2);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("OK test_offline_dictation\n");
  return 0;
}
