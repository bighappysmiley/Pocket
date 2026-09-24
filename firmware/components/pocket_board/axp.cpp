#include "pocket_board/axp.hpp"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_axp";
constexpr uint8_t kAxpAddr = 0x34;
constexpr int kSda = 41;
constexpr int kScl = 42;

// AXP2101 registers (XPowersLib / Waveshare)
constexpr uint8_t kRegDcOnOff = 0x80;
constexpr uint8_t kRegDc1Vol = 0x82;
constexpr uint8_t kRegLdoOnOff0 = 0x90;
constexpr uint8_t kRegAldo1Vol = 0x92;
constexpr uint8_t kRegAldo2Vol = 0x93;
constexpr uint8_t kRegAldo3Vol = 0x94;

i2c_master_bus_handle_t bus_ = nullptr;
i2c_master_dev_handle_t dev_ = nullptr;

bool wr(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  esp_err_t err = i2c_master_transmit(dev_, buf, sizeof(buf), 100);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "write 0x%02x=0x%02x failed: %s", reg, val, esp_err_to_name(err));
    return false;
  }
  return true;
}

bool rd(uint8_t reg, uint8_t* val) {
  esp_err_t err = i2c_master_transmit_receive(dev_, &reg, 1, val, 1, 100);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "read 0x%02x failed: %s", reg, esp_err_to_name(err));
    return false;
  }
  return true;
}

bool ensure_bus() {
  if (dev_) return true;

  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = static_cast<gpio_num_t>(kSda);
  bus_cfg.scl_io_num = static_cast<gpio_num_t>(kScl);
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  bus_cfg.flags.enable_internal_pullup = true;

  esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "i2c bus: %s", esp_err_to_name(err));
    return false;
  }

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = kAxpAddr;
  dev_cfg.scl_speed_hz = 400000;
  err = i2c_master_bus_add_device(bus_, &dev_cfg, &dev_);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "add axp: %s", esp_err_to_name(err));
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(20));
  return true;
}

}  // namespace

bool axp_enable_epd_rails() {
  if (!ensure_bus()) return false;

  uint8_t id = 0;
  if (!rd(0x03, &id)) {
    ESP_LOGW(TAG, "AXP2101 not responding — continuing without PMIC tweaks");
    return false;
  }
  ESP_LOGI(TAG, "AXP2101 chip id=0x%02x", id);

  // DC1 = 3.3V (ESP rail): (3300-1500)/100 = 18
  wr(kRegDc1Vol, 18);
  uint8_t dc = 0;
  if (rd(kRegDcOnOff, &dc)) wr(kRegDcOnOff, static_cast<uint8_t>(dc | 0x01));

  // ALDO1/2/3 = 3.3V: (3300-500)/100 = 28 — EPD_VCC_AXP on this board
  wr(kRegAldo1Vol, 28);
  wr(kRegAldo2Vol, 28);
  wr(kRegAldo3Vol, 28);
  uint8_t ldo = 0;
  if (rd(kRegLdoOnOff0, &ldo)) {
    // bit0=ALDO1, bit1=ALDO2, bit2=ALDO3 (XPowersLib)
    wr(kRegLdoOnOff0, static_cast<uint8_t>(ldo | 0x07));
  } else {
    wr(kRegLdoOnOff0, 0x07);
  }

  // Enable fuel gauge (REG 0x18 bit3) so battery % at 0xA4 is valid.
  uint8_t fg = 0;
  if (rd(0x18, &fg)) {
    wr(0x18, static_cast<uint8_t>(fg | 0x08));
  }

  vTaskDelay(pdMS_TO_TICKS(50));
  ESP_LOGI(TAG, "EPD power rails enabled (DC1 + ALDO1/2/3 @ 3.3V)");
  return true;
}

int axp_battery_percent() {
  if (!ensure_bus()) return 100;  // host/USB fallback when PMIC missing

  // Status1 @ 0x00: bit3=battery present, bit5=VBUS good
  uint8_t st = 0;
  const bool have_st = rd(0x00, &st);
  const bool bat_present = have_st && (st & 0x08);
  const bool vbus_good = have_st && (st & 0x20);

  if (!bat_present) {
    // USB-powered bring-up without a cell — show full.
    return vbus_good ? 100 : 0;
  }

  uint8_t pct = 0;
  if (!rd(0xA4, &pct)) {
    return vbus_good ? 100 : 50;
  }
  if (pct > 100) pct = 100;
  return static_cast<int>(pct);
}

}  // namespace pocket::board

extern "C" void pocket_axp_epd_power_on(void) {
  // Waveshare EPD_Init → EPD_Power_ON → enableALDO3(). Idempotent full bring-up.
  (void)pocket::board::axp_enable_epd_rails();
}
