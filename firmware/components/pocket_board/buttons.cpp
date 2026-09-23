#include "pocket_board/buttons.hpp"
#include "pocket_board/pins.hpp"

#include "driver/gpio.h"
#include "esp_log.h"

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_btn";

inline bool pressed(int pin) {
  // Active-low with pull-up (Waveshare button_bsp).
  return gpio_get_level(static_cast<gpio_num_t>(pin)) == 0;
}

}  // namespace

void ButtonPoller::init() {
  gpio_config_t io = {};
  io.intr_type = GPIO_INTR_DISABLE;
  io.mode = GPIO_MODE_INPUT;
  io.pin_bit_mask = (1ULL << kPinButtonUp) | (1ULL << kPinButtonDown) | (1ULL << kPinButtonFunction) |
                    (1ULL << kPinBoot) | (1ULL << kPinPwr);
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.pull_up_en = GPIO_PULLUP_ENABLE;
  ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&io));

  last_up_ = pressed(kPinButtonUp);
  last_down_ = pressed(kPinButtonDown);
  last_fn_ = pressed(kPinButtonFunction);
  last_boot_ = pressed(kPinBoot);
  last_pwr_ = pressed(kPinPwr);
  inited_ = true;
  ESP_LOGI(TAG, "buttons ready: Up=%d Fn=%d Down=%d BOOT=%d PWR=%d (active-low)", kPinButtonUp,
           kPinButtonFunction, kPinButtonDown, kPinBoot, kPinPwr);
}

void ButtonPoller::poll(pocket::InputMapper& mapper, uint32_t now_ms) {
  if (!inited_) return;

  const bool up = pressed(kPinButtonUp);
  const bool down = pressed(kPinButtonDown);
  const bool fn = pressed(kPinButtonFunction);
  const bool boot = pressed(kPinBoot);
  const bool pwr = pressed(kPinPwr);

  if (up != last_up_) {
    mapper.on_button_up(up, now_ms);
    last_up_ = up;
  }
  if (down != last_down_) {
    mapper.on_button_down(down, now_ms);
    last_down_ = down;
  }
  if (fn != last_fn_) {
    mapper.on_button_function(fn, now_ms);
    last_fn_ = fn;
  }
  if (boot != last_boot_) {
    mapper.on_boot(boot, now_ms);
    last_boot_ = boot;
  }
  if (pwr != last_pwr_) {
    mapper.on_pwr(pwr, now_ms);
    last_pwr_ = pwr;
  }

  mapper.tick(now_ms);
}

}  // namespace pocket::board
