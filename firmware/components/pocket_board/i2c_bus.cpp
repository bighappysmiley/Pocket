#include "pocket_board/i2c_bus.hpp"
#include "pocket_board/pins.hpp"

#include "esp_log.h"

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_i2c";
i2c_master_bus_handle_t bus_ = nullptr;

}  // namespace

i2c_master_bus_handle_t i2c_bus() {
  if (bus_) return bus_;
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(kPinI2cSda);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(kPinI2cScl);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;
  esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
    bus_ = nullptr;
  }
  return bus_;
}

}  // namespace pocket::board
