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
  bool start_provision(const std::string&, std::string*, std::string*) override { return true; }
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
  std::string books_list_json(const std::string&) override {
    return R"([{"id":"b1","title":"A Short Story","author":"Pocket","format":"epub","filename":"short.txt","size":900}])";
  }
  std::vector<uint8_t> book_download(const std::string&, const std::string&) override {
    // A few short paragraphs — enough to span more than one page.
    std::string body;
    for (int i = 0; i < 60; ++i) {
      body += "This is a line of sample book text for pagination testing. ";
    }
    return std::vector<uint8_t>(body.begin(), body.end());
  }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
};
struct TStorage : PlatformStorage {
  std::string root = "/tmp/pocket-books-test";
  std::string book_root() override { return root; }
  bool book_ensure_root() override {
    std::string cmd = "mkdir -p " + root;
    return std::system(cmd.c_str()) == 0;
  }
  uint64_t free_bytes(const std::string&) override { return 8ull * 1024 * 1024; }
};

int main() {
  MemoryConfigStore store;
  auto& cfg = store.mut();
  cfg.onboarding_complete = true;
  cfg.device_id = "dev-test";
  cfg.pin_length = 4;
  set_pin(cfg, "0000");

  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  TStorage storage;
  App app(store, clock, wifi, cloud, disp, &storage, nullptr);
  app.boot();

  // Reading defaults to visible on Home.
  CHECK(home_app_visible(app.config(), HomeApp::Reading));

  app.handle(InputEvent::Select);  // Lock -> Pin
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);

  // Display order: Notes Ledger Clock Pass Weather Music Reading Settings Update
  for (int i = 0; i < 6; ++i) app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::ReadingList);

  // Empty library → Sync row at index 0
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::ReadingList);

  // Open the synced book
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::ReadingBook);

  // Next page should advance without crashing and Back should return to the list.
  app.handle(InputEvent::Down);  // focus Next page
  app.handle(InputEvent::Select);
  app.handle(InputEvent::Back);
  CHECK(app.screen() == ScreenId::ReadingList);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_reading OK");
  return 0;
}
