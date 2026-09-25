#pragma once
#include <cstdint>
#include <vector>

namespace pocket::board {

enum class SoundId : uint8_t {
  Click = 0,
  Welcome,
  Success,
  Attention,
};

/** Init ES8311 + I2S + PA. Safe no-op if codec missing. */
bool audio_init();

/** Play a short UI sound (non-blocking best-effort; may block briefly). */
void audio_play(SoundId id);

/** Play a 16-bit PCM WAV (mono or stereo) from a filesystem path. */
bool audio_play_wav_file(const char* path);

/** Stop current playback stream (best-effort). */
void audio_stop();

/** DAC output level, 0–100. Clamped; applied immediately once the codec is ready. */
void audio_set_volume(uint8_t percent);

/** True once the codec + I2S RX path are ready to capture from the onboard mic. */
bool audio_mic_supported();

/** Begin a capture window (enables I2S RX). Call once on PTT down. */
void audio_capture_start();

/** Non-blocking: drain whatever samples the DMA has buffered since the last poll. Call from the
 * ~50 ms UI tick while held so nothing gets dropped between polls. */
void audio_capture_poll();

struct MicCaptureResult {
  bool ok = false;
  int level_percent = 0;
  std::vector<uint8_t> pcm;  // 16-bit mono PCM, 16 kHz, little-endian
};

/** End the capture window (disables I2S RX) and return everything captured. */
MicCaptureResult audio_capture_stop();

}  // namespace pocket::board
