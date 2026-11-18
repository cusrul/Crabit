// Compile-time crosscheck between the BW16 sender constants and the staged
// receiver contract. This only proves the two headers agree; it is not a wire
// test and does not build either firmware.
#include <stdint.h>
#include <string.h>

#include "app/compat/arduboy/ArduboyAudioCompat.h"
#include "shared/game_tone.h"

namespace audio = CrabitArduboyAudio;

static_assert(audio::kMinFrequency == CrabitTone::kMinFrequencyHz,
              "BW16/STM32 minimum frequency drift");
static_assert(audio::kMaxFrequency == CrabitTone::kMaxFrequencyHz,
              "BW16/STM32 maximum frequency drift");
static_assert(audio::kMinDurationMs == CrabitTone::kMinDurationMs,
              "BW16/STM32 minimum duration drift");
static_assert(audio::kMaxDurationMs == CrabitTone::kMaxDurationMs,
              "BW16/STM32 maximum duration drift");
static_assert(audio::kMinStartSpacingMs * CrabitTone::kMaxStartsPerSecond == 1000,
              "50 ms spacing no longer means 20 starts/s");
static_assert(audio::kRefreshMs < CrabitTone::kLeaseMs,
              "refresh must land before the receiver lease expires");
static_assert(audio::kMaxDurationMs <= CrabitTone::kLeaseMs,
              "a cue must not outlive the receiver lease");

int main() {
    // Same topic text (pointer identity is not guaranteed across stages).
    return strcmp(CrabitTone::TOPIC, Topics::AppTone) == 0 ? 0 : 1;
}
