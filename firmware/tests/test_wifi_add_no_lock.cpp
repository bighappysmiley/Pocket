/** P0: Add network / SoftAP must not lock — idle, Power, or enter-path RF glitch.
 *
 * Repro: Settings → Wi‑Fi → Add network… → Another Wi‑Fi opens SoftAP then
 * immediately Lock (SoftAP RF + e-ink enter glitching PWR/BOOT, or Back clearing
 * session then Power→Lock). Enter screen must stay unlocked.
 */
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
  void local_hm(int& h, int& m, int& weekday, int& month, int& day) override {
    h = 10;
    m = 5;
    weekday = 1;
    month = 3;
    day = 24;
  }
  int battery_percent() override { return 80; }
};
struct TWifi : PlatformWifi {
  bool ok = false;
  bool provisioned = false;
  int start_calls = 0;
  int peek_calls = 0;
  /** Stay offline so Add network uses SoftAP (not Companion cloud hint). */
  bool allow_connect = false;
  std::vector<std::string> scan() override { return {}; }
  bool connect(const std::string&, const std::string&) override {
    if (!allow_connect) return false;
    ok = true;
    return true;
  }
  bool connected() const override { return ok; }
  bool peek_provision_identity(std::string* ap, std::string* pw) override {
    ++peek_calls;
    if (ap) *ap = "Pocket-TEST";
    if (pw) *pw = "AB23CD45";
    return true;
  }
  bool start_provision(const std::string&, std::string* ap, std::string* pw) override {
    ++start_calls;
    provisioned = true;
    if (ap) *ap = "Pocket-TEST";
    if (pw) *pw = "AB23CD45";
    return true;
  }
  void stop_provision() override { provisioned = false; }
  bool take_provision_credentials(std::string*, std::string*) override { return false; }
  std::string provision_ap_ssid() const override { return "Pocket-TEST"; }
  std::string provision_ap_password() const override { return "AB23CD45"; }
  bool provisioning() const override { return provisioned; }
};
struct TCloud : PlatformCloud {
  std::string create_pair_session(const std::string&) override { return {}; }
  std::string pair_status(const std::string&) override { return "pending"; }
  void refresh_entitlement(DeviceConfig&) override {}
  std::string stt_transcribe(const std::vector<uint8_t>&) override { return {}; }
};
struct TDisp : PlatformDisplay {
  int busy_left = 0;
  int present_calls = 0;
  /** When true, present kicks a short busy window (device SoftAP enter race). */
  bool kick_busy_on_present = true;
  void present(const Canvas&, RefreshMode) override {
    ++present_calls;
    if (kick_busy_on_present && busy_left == 0) busy_left = 2;
  }
  void present_region(const Canvas&, int, int, int, int) override {
    present(Canvas{}, RefreshMode::Partial);
  }
  bool busy() override { return busy_left > 0; }
  void advance() {
    if (busy_left > 0) --busy_left;
  }
};

static void unlock_home(App& app) {
  CHECK(app.screen() == ScreenId::Lock);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Pin);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);
}

static void goto_wifi_add_chooser(App& app, TClock& clock, TWifi& wifi, TDisp& disp) {
  while (app.home_focus_index() > 0) app.apply_home_focus_delta(-1);
  app.apply_home_focus_delta(7);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::SettingsRoot);

  app.handle(InputEvent::Down);  // Wi‑Fi
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::SettingsWifi);

  app.handle(InputEvent::Down);  // Add network…
  app.handle(InputEvent::Select);
  clock.t += 60 * 1000;
  while (disp.busy()) {
    disp.advance();
    app.tick(clock.t);
  }
  app.tick(clock.t);
  CHECK(app.screen() == ScreenId::SettingsWifi);
  CHECK(app.screen() != ScreenId::Lock);
  CHECK(!wifi.ok);
}

int main() {
  MemoryConfigStore store;
  store.mut().onboarding_complete = true;
  store.mut().pin_length = 4;
  set_pin(store.mut(), "0000");
  store.mut().home_visible = 0x01FF;
  store.mut().idle_lock_s = 30;
  store.mut().device_id = "00000000-0000-4000-8000-000000000100";
  store.mut().device_name = "Pocket";
  store.mut().wifi_ssid = "OldNet";
  wifi_known_upsert(store.mut(), "OldNet", "secret");

  TClock clock;
  TWifi wifi;
  wifi.ok = false;  // offline → SoftAP add path
  TCloud cloud;
  TDisp disp;
  App app(store, clock, wifi, cloud, disp);
  app.boot();
  while (disp.busy()) {
    disp.advance();
    app.tick(clock.t);
  }
  unlock_home(app);

  goto_wifi_add_chooser(app, clock, wifi, disp);

  // --- Enter SoftAP while panel busy (device e-ink race): paint first, RF deferred ---
  disp.busy_left = 0;  // next present will set busy
  const int starts_before = wifi.start_calls;
  app.handle(InputEvent::Select);  // Personal Hotspot / Another Wi‑Fi → SoftAP
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(app.screen() != ScreenId::Lock);
  CHECK(wifi.peek_calls >= 1);
  // Radio must wait until SoftAP enter paint finishes.
  CHECK(wifi.start_calls == starts_before);
  CHECK(disp.busy());

  // Immediate Power / Back / Home on enter — must NOT Lock (RF glitch sim).
  app.handle(InputEvent::Power);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(app.screen() != ScreenId::Lock);
  app.handle(InputEvent::Back);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  app.handle(InputEvent::Home);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);

  // Finish SoftAP paint → deferred radio starts.
  disp.advance();
  disp.advance();
  clock.t += 50;
  app.tick(clock.t);
  CHECK(wifi.start_calls == starts_before + 1);
  CHECK(wifi.provisioned);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(app.screen() != ScreenId::Lock);

  // Spurious gestures right as RF comes up.
  app.handle(InputEvent::Power);
  app.handle(InputEvent::Back);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(app.screen() != ScreenId::Lock);

  // Idle far beyond idle_lock_s — SoftAP must remain.
  clock.t += 5 * 60 * 1000;
  app.tick(clock.t);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(app.screen() != ScreenId::Lock);
  CHECK(wifi.provisioned);

  // Cancel → Settings Wi‑Fi.
  app.handle(InputEvent::Down);
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::SettingsWifi);
  CHECK(!wifi.provisioned);

  // After Cancel + RF guard expiry, Power locks again.
  clock.t += 4000;
  app.tick(clock.t);
  app.handle(InputEvent::Power);
  CHECK(app.screen() == ScreenId::Lock);

  // Unlock; outside add flow, idle still locks.
  app.handle(InputEvent::Select);
  for (int i = 0; i < 4; ++i) app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::Home);
  app.handle(InputEvent::Home);
  CHECK(app.screen() == ScreenId::Home);
  clock.t += 60 * 1000;
  app.tick(clock.t);
  CHECK(app.screen() == ScreenId::Lock);

  // --- Second enter: panel idle (host path) — SoftAP radio starts in begin_add ---
  unlock_home(app);
  goto_wifi_add_chooser(app, clock, wifi, disp);
  while (disp.busy()) {
    disp.advance();
    app.tick(clock.t);
  }
  // Force idle so begin_add starts radio synchronously after paint.
  disp.kick_busy_on_present = false;
  disp.busy_left = 0;
  const int starts2 = wifi.start_calls;
  app.handle(InputEvent::Select);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);
  CHECK(wifi.start_calls == starts2 + 1);
  app.handle(InputEvent::Power);
  app.handle(InputEvent::Back);
  CHECK(app.screen() != ScreenId::Lock);
  CHECK(app.screen() == ScreenId::OnboardingWifiPassword);

  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("ok test_wifi_add_no_lock\n");
  return 0;
}
