#include "pocket/input.hpp"
#include <cassert>
#include <cstdio>

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
  InputMapper m;
  // Function short → Select
  m.on_button_function(true, 0);
  m.on_button_function(false, 100);
  CHECK(m.poll() == InputEvent::Select);
  CHECK(m.poll() == InputEvent::None);

  // Function long → Home (arm via tick)
  m.on_button_function(true, 1000);
  m.tick(1000 + 850);
  CHECK(m.poll() == InputEvent::Home);
  m.on_button_function(false, 2000);
  CHECK(m.poll() == InputEvent::None);  // no Select after long

  // BOOT short → Back
  m.on_boot(true, 3000);
  m.on_boot(false, 3100);
  CHECK(m.poll() == InputEvent::Back);

  // BOOT hold → PTT start, release → PTT stop (no Back)
  m.on_boot(true, 4000);
  m.tick(4250);
  CHECK(m.poll() == InputEvent::PttStart);
  m.on_boot(false, 5000);
  CHECK(m.poll() == InputEvent::PttStop);
  CHECK(m.poll() == InputEvent::None);

  // Up/Down
  m.on_button_up(true, 6000);
  m.on_button_down(true, 6001);
  CHECK(m.poll() == InputEvent::Up);
  CHECK(m.poll() == InputEvent::Down);

  // Mechanical bounce: a single physical Up press chatters several raw
  // press/release edges in quick succession (no hardware debounce on this
  // board). That must collapse to exactly one Up event, not one per edge —
  // otherwise every bounce turns into its own screen refresh ("refreshes
  // 500 times instead of once").
  m.on_button_up(true, 7000);
  m.on_button_up(false, 7004);
  m.on_button_up(true, 7009);
  m.on_button_up(false, 7015);
  m.on_button_up(true, 7022);
  CHECK(m.poll() == InputEvent::Up);
  CHECK(m.poll() == InputEvent::None);
  // Once the bounce settles and the debounce window elapses, the next real
  // press is accepted normally.
  m.on_button_up(true, 7200);
  CHECK(m.poll() == InputEvent::Up);
  CHECK(m.poll() == InputEvent::None);

  // Same for the Function (Select) button: bounce right after the press must
  // not be mistaken for the release (each would push a full-screen
  // navigation/refresh). Chatter inside the debounce window is dropped; the
  // real release outside that window still fires exactly one Select.
  m.on_button_function(true, 8000);
  m.on_button_function(false, 8005);
  m.on_button_function(true, 8010);
  m.on_button_function(false, 8016);
  m.on_button_function(false, 8100);  // real release, well past the debounce window
  CHECK(m.poll() == InputEvent::Select);
  CHECK(m.poll() == InputEvent::None);

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_input OK");
  return 0;
}