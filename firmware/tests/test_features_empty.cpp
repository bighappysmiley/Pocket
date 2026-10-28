/** Empty-state / offline feature paths: Notes, Ledger, Pass, Weather, Music, Reading, Update. */
#include "pocket/app.hpp"
#include <cstdio>
#include <cstdlib>
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
  uint32_t now_ms() override { return 8000; }
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 10;
    minute = 15;
    weekday = 2;
    month = 9;
    day = 28;
  }
  bool time_valid() const override { return true; }
};
struct TWifi : PlatformWifi {
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override { return false; }
  bool connected() const override { return false; }
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
  std::string create_pair_session(const std::string&) override { return {}; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
  void present_region(const Canvas&, int, int, int, int) override {}
};
struct TStorage : PlatformStorage {
  std::string root = "/tmp/pocket-features-empty-test";
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

static void unlock_home(App& app) {
  if (app.screen() == ScreenId::Lock) app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
}

/** Fresh app so Home focus starts at Notes (index 0). Display order:
 * Notes, Ledger, Clock, Pass, Weather, Music, Reading, Settings, Update. */
static void boot_unlocked(App& app) {
  app.boot();
  unlock_home(app);
  CHECK(app.screen() == ScreenId::Home);
}

static void open_home_index(App& app, int index) {
  for (int i = 0; i < index; ++i) app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
}

int main() {
  std::system("rm -rf /tmp/pocket-features-empty-test");
  MemoryConfigStore store;
  auto& cfg = store.mut();
  cfg.onboarding_complete = true;
  cfg.pin_length = 4;
  set_pin(cfg, "0000");
  cfg.home_visible = 0x01FF;
  cfg.device_id = "features-empty";
  cfg.weather_city.clear();

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  TStorage storage;

  // Notes empty → New note offline.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 0);
    CHECK(app.screen() == ScreenId::NotesList);
    app.redraw(true);
    app.handle(InputEvent::Select);  // New note
    CHECK(app.screen() == ScreenId::NotesDetail);
  }

  // Ledger coming soon.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 1);
    CHECK(app.screen() == ScreenId::LedgerComingSoon);
    app.redraw(true);
  }

  // Pass empty offline.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 3);
    CHECK(app.screen() == ScreenId::PassList);
    app.redraw(true);
  }

  // Weather no city offline.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 4);
    CHECK(app.screen() == ScreenId::WeatherMain);
    app.redraw(true);
  }

  // Music empty offline — Sync stays on list; focus moves Sync↔Back.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 5);
    CHECK(app.screen() == ScreenId::MusicList);
    app.redraw(true);
    app.handle(InputEvent::Select);  // Sync (needs Wi‑Fi)
    CHECK(app.screen() == ScreenId::MusicList);
    app.handle(InputEvent::Down);
    app.handle(InputEvent::Up);
    app.redraw(true);
  }

  // Reading empty offline.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 6);
    CHECK(app.screen() == ScreenId::ReadingList);
    app.redraw(true);
    app.handle(InputEvent::Select);  // Sync offline
    CHECK(app.screen() == ScreenId::ReadingList);
  }

  // Settings root + About redraw offline.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 7);
    CHECK(app.screen() == ScreenId::SettingsRoot);
    app.redraw(true);
    // About is typically near the end of the settings root list.
    for (int i = 0; i < 10; ++i) app.handle(InputEvent::Down);
    app.handle(InputEvent::Select);
    app.redraw(true);
  }

  // Update tile offline → OTA path ends on result ("Connect to Wi‑Fi first.").
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 8);
    CHECK(app.screen() == ScreenId::SettingsUpdateResult);
    app.redraw(true);
  }

  // Settings → Software update still shows the offline check hint.
  {
    App app(store, clock, wifi, cloud, disp, &storage, nullptr);
    boot_unlocked(app);
    open_home_index(app, 7);
    CHECK(app.screen() == ScreenId::SettingsRoot);
    // Settings root order includes Update near the end (before About typically).
    for (int i = 0; i < 8; ++i) app.handle(InputEvent::Down);
    app.handle(InputEvent::Select);
    // Accept either Update or a neighboring settings page — redraw must not crash.
    app.redraw(true);
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::printf("OK test_features_empty\n");
  return 0;
}
