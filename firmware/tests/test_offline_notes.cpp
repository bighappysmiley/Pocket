/** Offline Notes: list / open / save via local storage without Wi‑Fi. */
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
  bool valid = true;
  uint32_t now_ms() override { return 5000; }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 9;
    minute = 41;
    weekday = 3;
    month = 9;
    day = 7;
  }
  bool time_valid() const override { return valid; }
};
struct TWifi : PlatformWifi {
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return false; }
  bool connected() const override { return false; }  // offline
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
  std::string create_pair_session(const std::string&) override { return {}; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override {
    ++stt_calls;
    return "should-not-run-offline";
  }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
};
struct TStorage : PlatformStorage {
  std::string root = "/tmp/pocket-offline-notes-test";
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
  uint64_t free_bytes(const std::string&) override { return 4ull * 1024 * 1024; }
};

struct TAudio : PlatformAudio {
  int play_file_calls = 0;
  std::string last_path;
  bool play_file(const std::string& path) override {
    ++play_file_calls;
    last_path = path;
    return !path.empty();
  }
  void stop() override {}
};

static void unlock_home(App& app) {
  if (app.screen() == ScreenId::Lock) app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
}

int main() {
  std::system("rm -rf /tmp/pocket-offline-notes-test");
  MemoryConfigStore store;
  auto& cfg = store.mut();
  cfg.onboarding_complete = true;
  cfg.pin_length = 4;
  set_pin(cfg, "0000");
  cfg.home_visible = 0x01FF;
  cfg.device_id = "offline-test";

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

  // Launch Notes (slot 0).
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::NotesList);

  // New note while offline — must persist locally, no STT.
  app.handle(InputEvent::Select);  // New note FAB when empty
  CHECK(app.screen() == ScreenId::NotesDetail);
  CHECK(cloud.stt_calls == 0);

  // File on disk
  {
    std::ifstream in(storage.root + "/notes/index.json");
    CHECK(static_cast<bool>(in));
    std::string body((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(body.find("\"title\":\"Note\"") != std::string::npos);
  }

  // Reboot app offline — note still lists.
  TAudio audio2;
  App app2(store, clock, wifi, cloud, disp, &storage, &audio2);
  app2.boot();
  unlock_home(app2);
  app2.handle(InputEvent::Select);  // Notes
  CHECK(app2.screen() == ScreenId::NotesList);
  // Open first note
  app2.handle(InputEvent::Select);
  CHECK(app2.screen() == ScreenId::NotesDetail);
  CHECK(cloud.stt_calls == 0);

  // Clock face works offline (local RTC).
  app2.handle(InputEvent::Back);
  app2.handle(InputEvent::Back);  // Home
  // Focus Clock (index 2 in default order Notes,Ledger,Clock…)
  app2.handle(InputEvent::Down);
  app2.handle(InputEvent::Down);
  app2.handle(InputEvent::Select);
  CHECK(app2.screen() == ScreenId::ClockFace);
  app2.redraw(true);

  // Settings root works offline (Display/PIN/About — no net required).
  app2.handle(InputEvent::Back);
  // Home order: Notes,Ledger,Clock,Pass,Weather,Music,Reading,Settings,Update
  for (int i = 0; i < 7; ++i) app2.handle(InputEvent::Down);
  app2.handle(InputEvent::Select);
  CHECK(app2.screen() == ScreenId::SettingsRoot);
  app2.redraw(true);
  app2.handle(InputEvent::Back);
  CHECK(app2.screen() == ScreenId::Home);

  // Seed a local track + book, then open Music/Reading offline.
  {
    storage.music_ensure_root();
    storage.book_ensure_root();
    {
      std::ofstream out(storage.music_root() + "/calm.wav", std::ios::binary);
      const char hdr[12] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E'};
      out.write(hdr, 12);
    }
    {
      std::ofstream out(storage.book_root() + "/essay.txt");
      out << "A quiet page that opens offline.\n";
    }
  }
  TAudio audio3;
  App app3(store, clock, wifi, cloud, disp, &storage, &audio3);
  app3.boot();
  unlock_home(app3);
  // Music is index 5
  for (int i = 0; i < 5; ++i) app3.handle(InputEvent::Down);
  app3.handle(InputEvent::Select);
  CHECK(app3.screen() == ScreenId::MusicList);
  app3.handle(InputEvent::Select);  // play first local track
  CHECK(app3.screen() == ScreenId::MusicNowPlaying);
  CHECK(audio3.play_file_calls == 1);
  CHECK(audio3.last_path.find("calm.wav") != std::string::npos);
  app3.handle(InputEvent::Back);
  app3.handle(InputEvent::Back);  // Home

  // Reading is index 6
  for (int i = 0; i < 6; ++i) app3.handle(InputEvent::Down);
  app3.handle(InputEvent::Select);
  CHECK(app3.screen() == ScreenId::ReadingList);
  app3.handle(InputEvent::Select);  // open first local book
  CHECK(app3.screen() == ScreenId::ReadingBook);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("OK test_offline_notes\n");
  return 0;
}
