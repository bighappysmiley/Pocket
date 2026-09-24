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
/** 16 kHz × MCLK_MULTIPLE_256 = 4.096 MHz — matches ES8311 coeff table. */
constexpr int kSampleRate = 16000;
constexpr int kMclkHz = kSampleRate * 256;

i2c_master_dev_handle_t es_dev_ = nullptr;
i2s_chan_handle_t tx_ = nullptr;
bool ready_ = false;

bool es_wr(uint8_t reg, uint8_t val) {
  if (!es_dev_) return false;
  uint8_t buf[2] = {reg, val};
  const esp_err_t err = i2c_master_transmit(es_dev_, buf, 2, 100);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "ES8311 wr 0x%02x=0x%02x failed: %s", reg, val, esp_err_to_name(err));
    return false;
  }
  return true;
}

bool es_rd(uint8_t reg, uint8_t* val) {
  if (!es_dev_ || !val) return false;
  return i2c_master_transmit_receive(es_dev_, &reg, 1, val, 1, 100) == ESP_OK;
}

bool es_rmw(uint8_t reg, uint8_t clear_mask, uint8_t set_bits) {
  uint8_t v = 0;
  if (!es_rd(reg, &v)) return false;
  v = static_cast<uint8_t>((v & ~clear_mask) | set_bits);
  return es_wr(reg, v);
}

/**
 * Full ES8311 DAC bring-up (Waveshare / Espressif es8311_init sequence).
 * Coeff for mclk=4096000, rate=16000: pre_div=1, pre_multi=0, adc/dac_div=1,
 * fs_mode=0, lrck_h=0, lrck_l=0xff, bclk_div=4, osr=0x10.
 */
bool es_init_regs() {
  // Reset → idle → power-on CSM
  if (!es_wr(0x00, 0x1F)) return false;
  vTaskDelay(pdMS_TO_TICKS(20));
  if (!es_wr(0x00, 0x00)) return false;
  if (!es_wr(0x00, 0x80)) return false;  // Power-on command

  // Clocks: enable all; MCLK from pin (not BCLK)
  if (!es_wr(0x01, 0x3F)) return false;

  // REG02: pre_div-1 in [7:5], pre_multi in [4:3]; keep low 3 bits
  uint8_t reg02 = 0;
  if (!es_rd(0x02, &reg02)) reg02 = 0;
  reg02 = static_cast<uint8_t>((reg02 & 0x07) | ((1 - 1) << 5) | (0 << 3));
  if (!es_wr(0x02, reg02)) return false;

  if (!es_wr(0x03, 0x10)) return false;  // fs_mode | adc_osr
  if (!es_wr(0x04, 0x10)) return false;  // dac_osr
  if (!es_wr(0x05, 0x00)) return false;  // adc_div/dac_div = 1

  uint8_t reg06 = 0;
  if (!es_rd(0x06, &reg06)) reg06 = 0;
  reg06 = static_cast<uint8_t>((reg06 & 0xE0) | (4 - 1));  // bclk_div=4, clear invert
  if (!es_wr(0x06, reg06)) return false;

  uint8_t reg07 = 0;
  if (!es_rd(0x07, &reg07)) reg07 = 0;
  reg07 = static_cast<uint8_t>((reg07 & 0xC0) | 0x00);  // lrck_h
  if (!es_wr(0x07, reg07)) return false;
  if (!es_wr(0x08, 0xFF)) return false;  // lrck_l

  // Slave I2S, 16-bit SDP (resolution code 3 << 2)
  uint8_t reg00 = 0;
  if (!es_rd(0x00, &reg00)) reg00 = 0x80;
  if (!es_wr(0x00, static_cast<uint8_t>(reg00 & ~0x40))) return false;
  if (!es_wr(0x09, 0x0C)) return false;  // SDP in 16-bit
  if (!es_wr(0x0A, 0x0C)) return false;  // SDP out 16-bit

  // Analog / DAC / headphone drive — the audible path
  if (!es_wr(0x0D, 0x01)) return false;  // power up analog
  if (!es_wr(0x0E, 0x02)) return false;  // PGA / ADC modulator
  if (!es_wr(0x12, 0x00)) return false;  // power-up DAC
  if (!es_wr(0x13, 0x10)) return false;  // enable HP drive
  if (!es_wr(0x1C, 0x6A)) return false;  // ADC EQ bypass
  if (!es_wr(0x37, 0x08)) return false;  // DAC EQ bypass

  // Volume ~75% on DAC_REG32; unmute DAC_REG31
  if (!es_wr(0x32, 0xBF)) return false;
  if (!es_rmw(0x31, 0x60, 0x00)) return false;  // clear mute bits 6|5

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
  // Stereo L=R avoids mono left/right slot mismatch on this codec path.
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
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

void write_tone(float freq_hz, int ms, float amplitude = 0.28f) {
  if (!tx_) return;
  const int n = (kSampleRate * ms) / 1000;
  // Interleaved stereo [L, R]
  std::vector<int16_t> buf(static_cast<size_t>(n) * 2);
  const int fade = std::min(120, n / 4);
  for (int i = 0; i < n; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
    float env = 1.f;
    if (i < fade) env = static_cast<float>(i) / static_cast<float>(fade);
    if (i > n - fade) env = static_cast<float>(n - i) / static_cast<float>(fade);
    const int16_t s =
        static_cast<int16_t>(amplitude * env * 32767.f * std::sin(2.f * static_cast<float>(M_PI) * freq_hz * t));
    buf[static_cast<size_t>(i) * 2] = s;
    buf[static_cast<size_t>(i) * 2 + 1] = s;
  }
  size_t written = 0;
  i2s_channel_write(tx_, buf.data(), buf.size() * sizeof(int16_t), &written, pdMS_TO_TICKS(ms + 80));
  // Let DMA finish so PA doesn't cut the tail.
  vTaskDelay(pdMS_TO_TICKS(20));
}

}  // namespace

bool audio_init() {
  if (ready_) return true;

  gpio_config_t io = {};
  io.mode = GPIO_MODE_OUTPUT;
  io.pin_bit_mask = 1ULL << kPinPaCtrl;
  gpio_config(&io);
  gpio_set_level(static_cast<gpio_num_t>(kPinPaCtrl), 1);
  vTaskDelay(pdMS_TO_TICKS(100));  // PA settle before codec open

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

  uint8_t reg = 0x00, val = 0;
  if (i2c_master_transmit_receive(es_dev_, &reg, 1, &val, 1, 100) != ESP_OK) {
    ESP_LOGW(TAG, "ES8311 not responding");
    return false;
  }

  if (!es_init_regs()) {
    ESP_LOGW(TAG, "ES8311 register init failed");
    return false;
  }
  vTaskDelay(pdMS_TO_TICKS(50));

  if (!i2s_init()) {
    ESP_LOGW(TAG, "I2S init failed");
    return false;
  }
  ready_ = true;
  ESP_LOGI(TAG, "audio ready (mclk=%d rate=%d)", kMclkHz, kSampleRate);
  return true;
}

void audio_play(SoundId id) {
  if (!ready_ && !audio_init()) return;
  switch (id) {
    case SoundId::Click:
      write_tone(880.f, 45, 0.22f);
      break;
    case SoundId::Welcome:
      write_tone(523.25f, 140, 0.28f);
      write_tone(659.25f, 140, 0.28f);
      write_tone(783.99f, 220, 0.30f);
      break;
    case SoundId::Success:
      write_tone(660.f, 100, 0.26f);
      write_tone(880.f, 160, 0.28f);
      break;
    case SoundId::Attention:
      write_tone(440.f, 90, 0.24f);
      vTaskDelay(pdMS_TO_TICKS(40));
      write_tone(440.f, 90, 0.24f);
      break;
  }
}

}  // namespace pocket::board
