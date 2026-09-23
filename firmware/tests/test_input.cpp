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

  if (failures) {
    std::printf("%d failures\n", failures);
    return 1;
  }
  std::puts("test_input OK");
  return 0;
}