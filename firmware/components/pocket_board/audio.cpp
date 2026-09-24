#include "pocket_board/audio.hpp"
#include "pocket_board/i2c_bus.hpp"
#include "pocket_board/pins.hpp"

#include <cmath>
#include <cstring>
#include <vector>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace pocket::board {
namespace {

constexpr const char* TAG = "pocket_audio";
constexpr uint8_t kEs8311Addr = 0x18;
constexpr int kSampleRate = 16000;

i2c_master_dev_handle_t es_dev_ = nullptr;
i2s_chan_handle_t tx_ = nullptr;
bool ready_ = false;

bool es_wr(uint8_t reg, uint8_t val) {
  if (!es_dev_) return false;
  uint8_t buf[2] = {reg, val};
  return i2c_master_transmit(es_dev_, buf, 2, 100) == ESP_OK;
}

bool es_init_regs() {
  // Minimal ES8311 bring-up for DAC playback (Waveshare / Espressif patterns).
  es_wr(0x00, 0x1F);  // reset
  vTaskDelay(pdMS_TO_TICKS(20));
  es_wr(0x00, 0x00);
  es_wr(0x01, 0x30);  // power up analog
  es_wr(0x02, 0x10);  // power up digital
  es_wr(0x16, 0x24);  // clock
  es_wr(0x04, 0x10);  // DAC power
  es_wr(0x05, 0x00);
  es_wr(0x0B, 0x00);  // ADC/DAC format I2S 16-bit
  es_wr(0x0C, 0x00);
  es_wr(0x10, 0x1F);  // DAC volume-ish
  es_wr(0x12, 0x00);
  es_wr(0x17, 0xBF);  // DAC enable
  return true;
}

bool i2s_init() {
  if (tx_) return true;
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;
  if (i2s_new_channel(&chan, &tx_, nullptr) != ESP_OK) return false;

  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  std.gpio_cfg.mclk = static_cast<gpio_num_t>(kPinI2sMclk);
  std.gpio_cfg.bclk = static_cast<gpio_num_t>(kPinI2sBclk);
  std.gpio_cfg.ws = static_cast<gpio_num_t>(kPinI2sWs);
  std.gpio_cfg.dout = static_cast<gpio_num_t>(kPinI2sDout);
  std.gpio_cfg.din = I2S_GPIO_UNUSED;
  std.gpio_cfg.invert_flags.mclk_inv = false;
  std.gpio_cfg.invert_flags.bclk_inv = false;
  std.gpio_cfg.invert_flags.ws_inv = false;

  if (i2s_channel_init_std_mode(tx_, &std) != ESP_OK) return false;
  if (i2s_channel_enable(tx_) != ESP_OK) return false;
  return true;
}

void write_tone(float freq_hz, int ms, float amplitude = 0.25f) {
  if (!tx_) return;
  const int n = (kSampleRate * ms) / 1000;
  std::vector<int16_t> buf(static_cast<size_t>(n));
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
    float env = 1.f;
    if (i < 80) env = static_cast<float>(i) / 80.f;
    if (i > n - 80) env = static_cast<float>(n - i) / 80.f;
    buf[static_cast<size_t>(i)] =
        static_cast<int16_t>(amplitude * env * 32767.f * std::sin(2.f * static_cast<float>(M_PI) * freq_hz * t));
  }
  size_t written = 0;
  i2s_channel_write(tx_, buf.data(), buf.size() * sizeof(int16_t), &written, pdMS_TO_TICKS(ms + 50));
}

}  // namespace

bool audio_init() {
  if (ready_) return true;

  gpio_config_t io = {};
  io.mode = GPIO_MODE_OUTPUT;
  io.pin_bit_mask = 1ULL << kPinPaCtrl;
  gpio_config(&io);
  gpio_set_level(static_cast<gpio_num_t>(kPinPaCtrl), 1);

  auto bus = i2c_bus();
  if (!bus) {
    ESP_LOGW(TAG, "no I2C bus");
    return false;
  }
  if (!es_dev_) {
    i2c_device_config_t dev = {};
    dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev.device_address = kEs8311Addr;
    dev.scl_speed_hz = 100000;
    if (i2c_master_bus_add_device(bus, &dev, &es_dev_) != ESP_OK) {
      ESP_LOGW(TAG, "ES8311 device add failed");
      es_dev_ = nullptr;
      return false;
    }
  }

  // Probe
  uint8_t reg = 0x00, val = 0;
  if (i2c_master_transmit_receive(es_dev_, &reg, 1, &val, 1, 100) != ESP_OK) {
    ESP_LOGW(TAG, "ES8311 not responding");
    return false;
  }

  es_init_regs();
  if (!i2s_init()) {
    ESP_LOGW(TAG, "I2S init failed");
    return false;
  }
  ready_ = true;
  ESP_LOGI(TAG, "audio ready");
  return true;
}

void audio_play(SoundId id) {
  if (!ready_ && !audio_init()) return;
  switch (id) {
    case SoundId::Click:
      write_tone(880.f, 40, 0.18f);
      break;
    case SoundId::Welcome:
      write_tone(523.25f, 120, 0.22f);
      write_tone(659.25f, 120, 0.22f);
      write_tone(783.99f, 180, 0.24f);
      break;
    case SoundId::Success:
      write_tone(660.f, 90, 0.22f);
      write_tone(880.f, 140, 0.24f);
      break;
    case SoundId::Attention:
      write_tone(440.f, 80, 0.2f);
      write_tone(440.f, 80, 0.2f);
      break;
  }
}

}  // namespace pocket::board
