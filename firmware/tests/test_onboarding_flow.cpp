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
  bool creds_ready = true;
  int connect_calls = 0;
  int start_calls = 0;
  bool fail_connect = false;
  std::string ap = "Pocket-TEST";
  std::string ap_pass = "AB23CD45";
  std::string pending_ssid = "NetA";
  std::string pending_pass = "secret";
  std::vector<std::string> scan() override { return {"NetA"}; }
  bool connect(const std::string&, const std::string& pw) override {
    ++connect_calls;
    if (fail_connect) {
      ok = false;
      return false;
    }
    ok = !pw.empty();
    if (ok) provisioned = false;  // SoftAP stopped after GOT_IP
    return ok;
  }
  bool connected() const override { return ok; }
  bool start_provision(const std::string& /*preferred*/, std::string* ap_ssid_out,
                       std::string* ap_pass_out) override {
    ++start_calls;
    if (provisioned) {
      if (ap_ssid_out) *ap_ssid_out = ap;
      if (ap_pass_out) *ap_pass_out = ap_pass;
      return true;
    }
    provisioned = true;
    if (ap_ssid_out) *ap_ssid_out = ap;
    if (ap_pass_out) *ap_pass_out = ap_pass;
    return true;
  }
  void stop_provision() override { provisioned = false; }
  bool take_provision_credentials(std::string* ssid, std::string* password) override {
    if (!provisioned || !creds_ready) return false;
    if (ssid) *ssid = pending_ssid;
    if (password) *password = pending_pass;
    creds_ready = false;
    return true;
  }
  std::string provision_ap_ssid() const override { return ap; }
  std::string provision_ap_password() const override { return ap_pass; }
  bool provisioning() const override { return provisioned; }
};
struct TCloud : PlatformCloud {
  std::string st = "pending";
  int creates = 0;
  std::string create_pair_session(const std::string&) override {
    ++creates;
    return "XYZW2345";
  }
  std::string pair_status(const std::string&) override { return st; }
  void refresh_entitlement(DeviceConfig& c) override { c.cloud_entitled = false; }
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return "test"; }
};
struct TDisp : PlatformDisplay {
  void present(const Canvas&, RefreshMode) override {}
};
struct TStorage : PlatformStorage {
  SdContentKind kind = SdContentKind::Absent;
  bool probe() override { return kind != SdContentKind::Absent; }
  bool present() override { return kind != SdContentKind::Absent; }
  SdContentKind classify() override { return kind; }
  bool erase_card() override {
    kind = SdContentKind::Empty;
    return true;
  }
  void unmount() override { kind = SdContentKind::Absent; }
};
struct TAudio : PlatformAudio {
  int plays = 0;
  void play(SoundId) override { ++plays; }
};

int main() {
  MemoryConfigStore store;
  TClock clock;
  TWifi wifi;
  TCloud cloud;
  TDisp disp;
  TStorage storage;
  TAudio audio;
  App app(store, clock, wifi, cloud, disp, &storage, &audio);
  app.boot();
  CHECK(app.screen() == ScreenId::OnboardingWelcome);
  CHECK(app.config().device_name == "Pocket");

  // Welcome → Download companion (required)
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingCompanionDownload);

  // Power must not jump to Lock during setup
  app.handle(InputEvent::Power);
  CHECK(app.screen() == ScreenId::OnboardingCompanionDownload);

  // Continue → SD gate
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingSdCard);

  // No card → Continue without card → SoftAP
  app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(wifi.provisioned);
  CHECK(!wifi.ap_pass.empty());
  CHECK(wifi.ap_pass.size() >= 8);

  // Phone posts credentials via SoftAP (polled in tick)
  clock.t += 500;
  app.tick(clock.t);
  CHECK(app.screen() == ScreenId::OnboardingCompanionQr);
  CHECK(cloud.creates >= 1);

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

  // Firmware-risk card forces erase
  {
    MemoryConfigStore store2;
    TClock clock2;
    TWifi wifi2;
    TCloud cloud2;
    TDisp disp2;
    TStorage storage2;
    storage2.kind = SdContentKind::FirmwareRisk;
    TAudio audio2;
    App app2(store2, clock2, wifi2, cloud2, disp2, &storage2, &audio2);
    app2.boot();
    app2.handle(InputEvent::Select);  // welcome → download
    app2.handle(InputEvent::Select);  // download → sd
    CHECK(app2.screen() == ScreenId::OnboardingSdCard);
    app2.handle(InputEvent::Select);  // erase
    CHECK(app2.screen() == ScreenId::OnboardingWifiPassword);
    CHECK(storage2.kind == SdContentKind::Absent);
  }

  // Already online after SD → skip SoftAP straight to pairing code
  {
    MemoryConfigStore store3;
    TClock clock3;
    TWifi wifi3;
    wifi3.ok = true;
    TCloud cloud3;
    TDisp disp3;
    TStorage storage3;
    TAudio audio3;
    App app3(store3, clock3, wifi3, cloud3, disp3, &storage3, &audio3);
    app3.boot();
    app3.handle(InputEvent::Select);  // welcome
    app3.handle(InputEvent::Select);  // download → sd
    CHECK(app3.screen() == ScreenId::OnboardingSdCard);
    app3.handle(InputEvent::Down);
    app3.handle(InputEvent::Select);  // continue without card
    CHECK(app3.screen() == ScreenId::OnboardingCompanionQr);
    CHECK(cloud3.creates >= 1);
    CHECK(!wifi3.provisioned);
  }

  // Failed SoftAP connect stays on SoftAP (no remount loop / second start unless needed)
  {
    MemoryConfigStore store4;
    TClock clock4;
    TWifi wifi4;
    wifi4.fail_connect = true;
    wifi4.creds_ready = true;
    TCloud cloud4;
    TDisp disp4;
    TStorage storage4;
    TAudio audio4;
    App app4(store4, clock4, wifi4, cloud4, disp4, &storage4, &audio4);
    app4.boot();
    app4.handle(InputEvent::Select);
    app4.handle(InputEvent::Select);
    app4.handle(InputEvent::Down);
    app4.handle(InputEvent::Select);
    CHECK(app4.screen() == ScreenId::OnboardingWifiPassword);
    const int starts = wifi4.start_calls;
    clock4.t += 500;
    app4.tick(clock4.t);
    CHECK(app4.screen() == ScreenId::OnboardingWifiPassword);
    CHECK(wifi4.connect_calls == 1);
    // SoftAP still up — idempotent start_provision, not a full remount restart
    CHECK(wifi4.provisioned);
    CHECK(wifi4.start_calls == starts);  // ensure_softap uses provisioning() → no new start
  }

  // Reboot mid-setup resumes past Welcome when progress was saved
  {
    MemoryConfigStore store5;
    store5.mut().wifi_ssid = "HomeNet";
    store5.mut().companion_linked = true;
    store5.mut().device_id = "00000000-0000-4000-8000-000000000099";
    TClock clock5;
    TWifi wifi5;
    TCloud cloud5;
    TDisp disp5;
    TStorage storage5;
    TAudio audio5;
    App app5(store5, clock5, wifi5, cloud5, disp5, &storage5, &audio5);
    app5.boot();
    CHECK(app5.screen() == ScreenId::OnboardingPinLength);
  }
  {
    MemoryConfigStore store6;
    store6.mut().wifi_ssid = "HomeNet";
    store6.mut().device_id = "00000000-0000-4000-8000-000000000098";
    TClock clock6;
    TWifi wifi6;
    TCloud cloud6;
    TDisp disp6;
    TStorage storage6;
    TAudio audio6;
    App app6(store6, clock6, wifi6, cloud6, disp6, &storage6, &audio6);
    app6.boot();
    CHECK(app6.screen() == ScreenId::OnboardingWifiPassword);
    CHECK(wifi6.provisioned);
  }
  {
    MemoryConfigStore store7;
    store7.mut().companion_linked = true;
    set_pin(store7.mut(), "1234");
    store7.mut().device_id = "00000000-0000-4000-8000-000000000097";
    TClock clock7;
    TWifi wifi7;
    TCloud cloud7;
    TDisp disp7;
    TStorage storage7;
    TAudio audio7;
    App app7(store7, clock7, wifi7, cloud7, disp7, &storage7, &audio7);
    app7.boot();
    CHECK(app7.screen() == ScreenId::OnboardingTimezone);
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_onboarding_flow OK");
  return 0;
}
