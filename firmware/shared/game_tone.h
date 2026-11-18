#pragma once
// Mono game-tone contract shared by BW16 and STM32.
//
// Topic: Topics::AppTone ("app/tone"), payload: two ASCII unsigned decimals
// separated by exactly one comma: "<frequency_hz>,<duration_ms>".
//   * request_id must be 0; delivery is best effort with no ACK.
//   * "0,0" is the stop command and bypasses the start rate limit.
//   * A cue is 100..5000 Hz for 10..250 ms.
//   * Anything else (sign, whitespace, extra comma, trailing junk, mixed zero
//     fields, out-of-range or overflowing values) is rejected.
//
// BW16 owns musical sequences, mixing and priority. Only one mono cue at a time
// crosses the wire; STM32 never queues a melody.

#include <stddef.h>
#include <stdint.h>

#include "topics.h"

namespace CrabitTone {

struct Tone {
    uint16_t frequency_hz = 0;
    uint16_t duration_ms = 0;
};

enum class ParseResult : uint8_t {
    Invalid = 0,
    Cue = 1,
    Stop = 2,
};

constexpr const char* TOPIC = Topics::AppTone;
constexpr uint16_t kMinFrequencyHz = 100;
constexpr uint16_t kMaxFrequencyHz = 5000;
constexpr uint16_t kMinDurationMs = 10;
constexpr uint16_t kMaxDurationMs = 250;

// Maximum age of a pending cue when take() accepts it, measured from
// publication. It is not the total audible time: an accepted cue may then play
// for up to kMaxDurationMs, so publication-to-silence can approach 500 ms.
// Session loss and stop still cancel earlier; this bound only prevents stale
// pending cues and hung/disconnect tones.
constexpr uint32_t kLeaseMs = 250;

// Starts admitted per fixed 1 s window. Stops do not consume this budget.
constexpr uint8_t kMaxStartsPerSecond = 20;

// Strict parser for exactly two unsigned decimal fields. `value` is not
// required to be NUL terminated; `length` is authoritative.
inline ParseResult parse(const char* value, size_t length, Tone* out) {
    if (value == nullptr || out == nullptr || length == 0) return ParseResult::Invalid;

    uint32_t fields[2] = {0, 0};
    size_t index = 0;
    for (unsigned field = 0; field < 2; ++field) {
        if (index >= length) return ParseResult::Invalid;
        if (value[index] < '0' || value[index] > '9') return ParseResult::Invalid;
        uint32_t parsed = 0;
        while (index < length && value[index] >= '0' && value[index] <= '9') {
            const uint32_t digit = static_cast<uint32_t>(value[index] - '0');
            // Reject anything that no longer fits an unsigned 16-bit field.
            if (parsed > (65535u - digit) / 10u) return ParseResult::Invalid;
            parsed = parsed * 10u + digit;
            ++index;
        }
        fields[field] = parsed;
        if (field == 0) {
            if (index >= length || value[index] != ',') return ParseResult::Invalid;
            ++index;
        }
    }
    if (index != length) return ParseResult::Invalid; // trailing junk or extra comma

    if (fields[0] == 0 && fields[1] == 0) return ParseResult::Stop;
    if (fields[0] < kMinFrequencyHz || fields[0] > kMaxFrequencyHz)
        return ParseResult::Invalid;
    if (fields[1] < kMinDurationMs || fields[1] > kMaxDurationMs)
        return ParseResult::Invalid;

    out->frequency_hz = static_cast<uint16_t>(fields[0]);
    out->duration_ms = static_cast<uint16_t>(fields[1]);
    return ParseResult::Cue;
}

} // namespace CrabitTone
