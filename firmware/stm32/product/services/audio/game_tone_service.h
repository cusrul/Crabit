#pragma once
// Minimal STM32 game-tone handoff.
//
// This is not a second audio path: Buzzer_Driver remains the only PWM owner and
// still consumes GlobalStore feedback first. The UART task only validates an
// app/tone frame and publishes the latest cue/stop plus its timestamp because the
// existing GlobalStore request fields are read-modify-written by the UI task and
// cannot be safely written from the UART task. The short platform critical
// section makes the word/timestamp pair one coherent snapshot; stop is a word
// value, not a new queue.
//
// Scheduling (priority, mute/session, rate limit, lease) lives in the existing
// Buzzer_Driver 10 ms update.

#include <stddef.h>
#include <stdint.h>

#include "game_tone.h"

namespace CrabitTone {

inline constexpr uint32_t kEmptyWord = 0u;
inline constexpr uint32_t kStopWord = 0xFFFFFFFFu;

inline uint32_t encode(const Tone& tone) {
    return (static_cast<uint32_t>(tone.frequency_hz) << 16) |
           static_cast<uint32_t>(tone.duration_ms);
}

inline Tone decode(uint32_t word) {
    Tone tone;
    tone.frequency_hz = static_cast<uint16_t>(word >> 16);
    tone.duration_ms = static_cast<uint16_t>(word & 0xFFFFu);
    return tone;
}

// UART task: validate one payload and publish the latest cue/stop. Returns false
// for an inactive BW16/coproc session, a nonzero request_id or an invalid
// payload. The topic is always consumed by the caller, so nothing is ACKed.
bool handleFrame(const char* value, size_t length, uint16_t request_id,
                 bool session_active, uint32_t now_ms);

// Buzzer_Driver task: take the latest word once. Returns 0 when nothing was
// published, or when a cue is older than the 250 ms lease. A stop is returned
// even when old: age must not swallow an explicit stop.
uint32_t take(uint32_t now_ms);

// Buzzer_Driver task: drop any pending cue (mute, session loss, user priority).
void discardPending();

// Bounded reset for tests and cold boot.
void reset();

} // namespace CrabitTone
