#pragma once
#include <cstdint>

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

}  // namespace pocket::board
