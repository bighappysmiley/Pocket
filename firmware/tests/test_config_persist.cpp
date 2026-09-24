#include "pocket/config.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
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

int main() {
  DeviceConfig cfg;
  cfg.device_name = "Pocket";
  cfg.onboarding_complete = true;
  cfg.companion_linked = true;
  cfg.pin_length = 4;
  set_pin(cfg, "1234");
  cfg.tz_id = "America/Los_Angeles";
  cfg.wifi_ssid = "HomeNet";
  wifi_known_upsert(cfg, "HomeNet", "secret1");
  wifi_known_upsert(cfg, "CafeWifi", "secret2");
  CHECK(cfg.wifi_known.size() == 2);
  CHECK(cfg.wifi_ssid == "CafeWifi");  // last upsert preferred
  CHECK(cfg.wifi_known[0].ssid == "CafeWifi");
  CHECK(cfg.wifi_known[0].password == "secret2");
  CHECK(cfg.wifi_known[1].ssid == "HomeNet");
  cfg.device_id = "aabbccdd-eeff-4000-8000-112233445566";
  cfg.device_token = "tok_test";
  cfg.cloud_status = "trialing";
  cfg.weather_city = "Seattle";
  cfg.weather_lat = 47.6f;
  cfg.weather_lon = -122.3f;
  cfg.idle_lock_s = 90;
  cfg.show_batt_pct = false;
  cfg.time_format = 24;

  std::vector<uint8_t> blob;
  CHECK(pack_device_config(cfg, blob));
  CHECK(!blob.empty());
  CHECK(blob.size() < 4096);

  DeviceConfig round;
  CHECK(unpack_device_config(blob.data(), blob.size(), round));
  CHECK(round.onboarding_complete);
  CHECK(round.companion_linked);
  CHECK(round.pin_length == 4);
  CHECK(verify_pin(round, "1234"));
  CHECK(!verify_pin(round, "0000"));
  CHECK(round.tz_id == "America/Los_Angeles");
  CHECK(round.wifi_ssid == "CafeWifi");
  CHECK(round.wifi_known.size() == 2);
  CHECK(round.wifi_known[0].ssid == "CafeWifi");
  CHECK(round.wifi_known[0].password == "secret2");
  CHECK(round.wifi_known[1].ssid == "HomeNet");
  CHECK(round.wifi_known[1].password == "secret1");
  CHECK(round.device_id == cfg.device_id);
  CHECK(round.device_token == "tok_test");
  CHECK(round.cloud_status == "trialing");
  CHECK(round.weather_city == "Seattle");
  CHECK(round.idle_lock_s == 90);
  CHECK(!round.show_batt_pct);
  CHECK(round.time_format == 24);

  // Corrupt magic → fail
  blob[0] ^= 0xff;
  DeviceConfig bad;
  CHECK(!unpack_device_config(blob.data(), blob.size(), bad));

  // File-backed store survives reload
  char path[] = "/tmp/pocket-cfg-XXXXXX";
  const int fd = ::mkstemp(path);
  CHECK(fd >= 0);
  if (fd >= 0) ::close(fd);

  {
    FileConfigStore store(path);
    store.save(cfg);
  }
  {
    FileConfigStore store(path);
    DeviceConfig loaded = store.load();
    CHECK(loaded.onboarding_complete);
    CHECK(loaded.wifi_ssid == "CafeWifi");
    CHECK(loaded.wifi_known.size() == 2);
    CHECK(loaded.wifi_known[1].password == "secret1");
    CHECK(verify_pin(loaded, "1234"));
    CHECK(loaded.device_id == cfg.device_id);
  }
  ::unlink(path);

  // Empty / missing file → defaults
  {
    FileConfigStore store("/tmp/pocket-cfg-does-not-exist-xyz");
    DeviceConfig d = store.load();
    CHECK(!d.onboarding_complete);
    CHECK(d.device_name == "Pocket");
  }

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_config_persist OK");
  return 0;
}
