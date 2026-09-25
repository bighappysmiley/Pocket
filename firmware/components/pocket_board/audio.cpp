#include "pocket_board/audio.hpp"
#include "pocket_board/i2c_bus.hpp"
#include "pocket_board/pins.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
i2s_chan_handle_t rx_ = nullptr;
bool ready_ = false;
bool capturing_ = false;
std::vector<uint8_t> capture_buf_;
constexpr size_t kMaxCaptureBytes = 16000 * 2 * 6;  // ~6 s mono @16 kHz

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

  // Mic (ADC) path — REG14 selects analog mic input + PGA gain, REG17 sets ADC volume.
  // Values match Espressif's es8311 driver defaults for a single-ended analog mic.
  if (!es_wr(0x14, 0x1A)) return false;  // analog mic1, PGA gain ~+18 dB
  if (!es_wr(0x17, 0xBF)) return false;  // ADC volume ~0 dB, unmuted

  return true;
}

bool i2s_init() {
  if (tx_) return true;
  // Duplex channel pair on I2S_NUM_0: tx_ drives the speaker continuously, rx_ (mic ADC data
  // from the same ES8311) is enabled only during a capture window to save power.
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;
  if (i2s_new_channel(&chan, &tx_, &rx_) != ESP_OK) return false;

  i2s_std_config_t std = {};
  std.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(kSampleRate);
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  // Stereo L=R avoids mono left/right slot mismatch on this codec path.
  std.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  std.gpio_cfg.mclk = static_cast<gpio_num_t>(kPinI2sMclk);
  std.gpio_cfg.bclk = static_cast<gpio_num_t>(kPinI2sBclk);
  std.gpio_cfg.ws = static_cast<gpio_num_t>(kPinI2sWs);
  std.gpio_cfg.dout = static_cast<gpio_num_t>(kPinI2sDout);
  std.gpio_cfg.din = static_cast<gpio_num_t>(kPinI2sDin);
  std.gpio_cfg.invert_flags.mclk_inv = false;
  std.gpio_cfg.invert_flags.bclk_inv = false;
  std.gpio_cfg.invert_flags.ws_inv = false;

  if (i2s_channel_init_std_mode(tx_, &std) != ESP_OK) return false;
  if (rx_ && i2s_channel_init_std_mode(rx_, &std) != ESP_OK) {
    ESP_LOGW(TAG, "mic RX channel init failed — playback still works");
    rx_ = nullptr;
  }
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

bool audio_play_wav_file(const char* path) {
  if (!path || !*path) return false;
  if (!ready_ && !audio_init()) return false;
  FILE* f = std::fopen(path, "rb");
  if (!f) {
    ESP_LOGW(TAG, "open failed: %s", path);
    return false;
  }
  uint8_t hdr[44] = {};
  if (std::fread(hdr, 1, 44, f) < 44) {
    std::fclose(f);
    return false;
  }
  if (std::memcmp(hdr, "RIFF", 4) != 0 || std::memcmp(hdr + 8, "WAVE", 4) != 0) {
    ESP_LOGW(TAG, "not WAV: %s", path);
    std::fclose(f);
    return false;
  }
  const uint16_t channels = static_cast<uint16_t>(hdr[22] | (hdr[23] << 8));
  const uint16_t bits = static_cast<uint16_t>(hdr[34] | (hdr[35] << 8));
  if (bits != 16 || (channels != 1 && channels != 2)) {
    ESP_LOGW(TAG, "need 16-bit mono/stereo WAV");
    std::fclose(f);
    return false;
  }
  constexpr size_t kChunk = 1024;
  std::vector<int16_t> in(kChunk);
  std::vector<int16_t> out(kChunk * 2);
  while (true) {
    const size_t nread = std::fread(in.data(), sizeof(int16_t), kChunk, f);
    if (nread == 0) break;
    size_t frames = nread;
    if (channels == 2) frames = nread / 2;
    for (size_t i = 0; i < frames; ++i) {
      int16_t l = 0, r = 0;
      if (channels == 1) {
        l = r = in[i];
      } else {
        l = in[i * 2];
        r = in[i * 2 + 1];
      }
      out[i * 2] = l;
      out[i * 2 + 1] = r;
    }
    size_t written = 0;
    i2s_channel_write(tx_, out.data(), frames * 2 * sizeof(int16_t), &written, pdMS_TO_TICKS(500));
  }
  std::fclose(f);
  vTaskDelay(pdMS_TO_TICKS(20));
  return true;
}

void audio_stop() {}

void audio_set_volume(uint8_t percent) {
  if (percent > 100) percent = 100;
  if (!ready_ && !audio_init()) return;
  // DAC_REG32 is linear 0..255; keep the same ~0.75 ceiling used at bring-up as "100%" so the
  // default doesn't clip on the small onboard speaker.
  const uint8_t reg = static_cast<uint8_t>((static_cast<int>(percent) * 0xBF) / 100);
  es_wr(0x32, reg);
}

bool audio_mic_supported() {
  if (!ready_ && !audio_init()) return false;
  return rx_ != nullptr;
}

void audio_capture_start() {
  if (!audio_mic_supported()) return;
  if (capturing_) return;
  capture_buf_.clear();
  capture_buf_.reserve(std::min(kMaxCaptureBytes, static_cast<size_t>(32000)));
  if (i2s_channel_enable(rx_) == ESP_OK) {
    capturing_ = true;
  }
}

void audio_capture_poll() {
  if (!capturing_ || !rx_) return;
  if (capture_buf_.size() >= kMaxCaptureBytes) return;
  uint8_t chunk[1024];
  size_t nread = 0;
  // Non-blocking best-effort drain; caller (App::tick) polls every ~50 ms.
  if (i2s_channel_read(rx_, chunk, sizeof(chunk), &nread, 0) == ESP_OK && nread > 0) {
    const size_t room = kMaxCaptureBytes - capture_buf_.size();
    const size_t take = std::min(nread, room);
    capture_buf_.insert(capture_buf_.end(), chunk, chunk + take);
  }
}

MicCaptureResult audio_capture_stop() {
  MicCaptureResult result;
  if (!capturing_) return result;
  // One last drain to catch anything still in the DMA buffer.
  audio_capture_poll();
  if (rx_) i2s_channel_disable(rx_);
  capturing_ = false;

  result.ok = !capture_buf_.empty();
  if (result.ok) {
    // Stereo interleaved 16-bit; fold to mono by taking the left channel — matches the
    // stereo-out slot config used for playback so we don't need a second slot layout.
    const auto* samples = reinterpret_cast<const int16_t*>(capture_buf_.data());
    const size_t n_frames = capture_buf_.size() / (2 * sizeof(int16_t));
    std::vector<int16_t> mono(n_frames);
    double sum_sq = 0.0;
    for (size_t i = 0; i < n_frames; ++i) {
      const int16_t s = samples[i * 2];
      mono[i] = s;
      sum_sq += static_cast<double>(s) * static_cast<double>(s);
    }
    result.pcm.resize(mono.size() * sizeof(int16_t));
    std::memcpy(result.pcm.data(), mono.data(), result.pcm.size());
    const double rms = n_frames ? std::sqrt(sum_sq / static_cast<double>(n_frames)) : 0.0;
    // 32767 full-scale → percent scale tuned so normal speech reads ~15-40%.
    result.level_percent = static_cast<int>(std::min(100.0, (rms / 32767.0) * 400.0));
  }
  capture_buf_.clear();
  return result;
}

}  // namespace pocket::board
