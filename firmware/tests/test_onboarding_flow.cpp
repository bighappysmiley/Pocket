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
  void local_hm(int& hour, int& minute, int& weekday, int& month, int& day) override {
    hour = 9;
    minute = 41;
    weekday = 3;
    month = 8;
    day = 23;
  }
};
struct TWifi : PlatformWifi {
  bool ok = false;
  std::vector<std::string> scan() override { return {"NetA"}; }
  bool connect(const std::string&, const std::string& pw) override {
    ok = !pw.empty();
    return ok;
  }
  bool connected() const override { return ok; }
};
struct TCloud : PlatformCloud {
  std::string st = "pending";
  std::string create_pair_session(const std::string&) override { return "XYZW2345"; }
  std::string pair_status(const std::string&) override { return st; }
  void refresh_entitlement(DeviceConfig& c) override { c.cloud_entitled = false; }
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return "test"; }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
};

int main() {
  MemoryConfigStore store;
  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  CHECK(app.screen() == ScreenId::OnboardingWelcome);
  CHECK(app.config().device_name == "Pocket");

  // Welcome → Wi‑Fi
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingWifiList);

  // Select network
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);

  // Enter one char, exit char-edit (Back), focus Connect, select
  app.handle(InputEvent::Select);  // enter char while editing picker
  app.handle(InputEvent::Back);    // leave char edit → action focus
  app.handle(InputEvent::Down);
  app.handle(InputEvent::Down);
  app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);  // Connect
  CHECK(app.screen() == ScreenId::OnboardingCompanionQr);

  // Skip for now
  app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingPinLength);

  // 4 digits
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingPinSet);

  // Enter 0000
  for (int i = 0; i < 4; ++i) {
    app.handle(InputEvent::Select);
  }
  CHECK(app.screen() == ScreenId::OnboardingPinConfirm);
  for (int i = 0; i < 4; ++i) {
    app.handle(InputEvent::Select);
  }
  CHECK(app.screen() == ScreenId::OnboardingTimezone);

  // Continue
  for (int i = 0; i < 9; ++i) app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingMicTest);

  app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingDone);

  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);
  CHECK(app.config().onboarding_complete);

  // No naming screen ever
  CHECK(app.config().device_name == "Pocket");

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_onboarding_flow OK");
  return 0;
}