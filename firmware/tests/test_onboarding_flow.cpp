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
  bool provisioned = false;
  std::string ap = "Pocket-TEST";
  std::string ap_pass = "AB23CD45";
  std::string pending_ssid = "NetA";
  std::string pending_pass = "secret";
  std::vector<std::string> scan() override { return {"NetA"}; }
  bool connect(const std::string&, const std::string& pw) override {
    ok = !pw.empty();
    return ok;
  }
  bool connected() const override { return ok; }
  bool start_provision(const std::string& /*preferred*/, std::string* ap_ssid_out,
                       std::string* ap_pass_out) override {
    provisioned = true;
    if (ap_ssid_out) *ap_ssid_out = ap;
    if (ap_pass_out) *ap_pass_out = ap_pass;
    return true;
  }
  void stop_provision() override { provisioned = false; }
  bool take_provision_credentials(std::string* ssid, std::string* password) override {
    if (!provisioned) return false;
    if (ssid) *ssid = pending_ssid;
    if (password) *password = pending_pass;
    provisioned = false;
    return true;
  }
  std::string provision_ap_ssid() const override { return ap; }
  std::string provision_ap_password() const override { return ap_pass; }
  bool provisioning() const override { return provisioned; }
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

  // Welcome → Download companion (required)
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingCompanionDownload);

  // Power must not jump to Lock during setup
  app.handle(InputEvent::Power);
  CHECK(app.screen() == ScreenId::OnboardingCompanionDownload);

  // Continue → Link SoftAP (unified Wi‑Fi + pair; no separate Wi‑Fi list)
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(wifi.provisioned);
  CHECK(!wifi.ap_pass.empty());
  CHECK(wifi.ap_pass.size() >= 8);

  // Phone posts credentials via SoftAP (polled in tick)
  clock.t += 500;
  app.tick(clock.t);
  CHECK(app.screen() == ScreenId::OnboardingCompanionQr);

  // Companion required: claim advances to PIN (no Skip)
  cloud.st = "claimed";
  clock.t += 3000;
  app.tick(clock.t);
  CHECK(app.screen() == ScreenId::OnboardingPinLength);
  CHECK(app.config().companion_linked);

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

  // --- Already on Wi‑Fi: Continue after companion download skips SoftAP ---
  {
    MemoryConfigStore store2;
    TClock clock2;
    TWifi wifi2;
    wifi2.ok = true;  // STA already connected
    TCloud cloud2;
    TDisp disp2;
    App app2(store2, clock2, wifi2, cloud2, disp2);
    app2.boot();
    app2.handle(InputEvent::Select);  // Welcome → Download
    CHECK(app2.screen() == ScreenId::OnboardingCompanionDownload);
    app2.handle(InputEvent::Select);  // Continue → pair QR (skip SoftAP)
    CHECK(app2.screen() == ScreenId::OnboardingCompanionQr);
    CHECK(!wifi2.provisioned);
    // Back while online returns to download (does not start SoftAP)
    app2.handle(InputEvent::Back);
    CHECK(app2.screen() == ScreenId::OnboardingCompanionDownload);
    CHECK(!wifi2.provisioned);
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_onboarding_flow OK");
  return 0;
}
