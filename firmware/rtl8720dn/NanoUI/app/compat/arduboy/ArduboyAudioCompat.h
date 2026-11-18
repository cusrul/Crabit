#ifndef CRABIT_ARDUBOY_AUDIO_COMPAT_H
#define CRABIT_ARDUBOY_AUDIO_COMPAT_H

// Mono game-audio adapter for the Arduboy compatibility ports.
//
// Scope: the BW16 has no local speaker. It turns the upstream Arduboy sound
// calls (ArduboyTones tone()/tones(), Playtune playScore()) into the existing
// STM32 PWM tone topic. Gameplay code only registers an intent here; one
// bounded tick (driven by ArduboyCompatApp::onUpdate, the existing frame API)
// parses the sequence and emits at most one "frequency,duration" request per
// tick through the existing Protocol outlet. There is no thread, RTOS queue,
// second scheduler or per-port audio engine.
//
// Wire contract (owned by the STM32 audio service):
//   topic Topics::AppTone ("app/tone"), ASCII "frequency,duration_ms",
//   frequency 100..5000 Hz, duration 10..250 ms, stop is "0,0",
//   request_id 0, no acknowledgement. At most 20 starts per second; the
//   sender coalesces and lets stop bypass the budget.
//
// Priority domains. The pinned OBONO MyArduboy sources (lasers, bananonsense,
// hopper, chiemagari, chribocchi, hollow, ...) use one uint8_t priority where a
// LOWER number is more important (`if (priority > current) return;`). The
// shared compat classes use their own small ladder (Score < Sequence < Effect,
// higher wins). These scales are not comparable, so intents carry a domain and
// are compared only within it. Across domains the fixed precedence is:
//   API Effect/Sequence  >  port raw priority  >  API Score
// Equal values replace, matching both upstream `priority >` checks.
//
// Termination is deliberately per port. ArduboyTones sequences normally end
// with TONES_END (0x8000) or TONES_REPEAT (0x8001); the AsteroidDodge port
// historically used 0 as its end marker, so callers state which marker applies
// instead of guessing from the data.
//
// Known mono flattening: Polyphonic Playtune channels collapse to one voice
// (the last note parsed before a wait wins), overlapping tones replace each
// other, the PWM has no volume control (the 0x8000 "high volume" bit is
// stripped), and frequencies outside 100..5000 Hz are clamped while notes
// longer than 250 ms are refreshed so they are not cut short by the contract.
//
// Memory contract: arrays/scores are trusted static, PROGMEM or RAM assets from
// the pinned corpus and must stay alive and terminated while a voice owns them.
// The caps below bound parsing work and limit how many words/bytes an
// unterminated asset can make the parser read before failing closed. They are
// NOT memory-safety bounds and cannot validate an arbitrary pointer. A
// length-carrying overload was not added because the upstream signatures erase
// the array size; wrappers and their static assets were intentionally not
// rewritten.
//
// Harness note: the host build has no Protocol. Tests install a capture sink
// with setSink(); the Arduino target sink is the real Protocol send, not a
// silent default.

#include <stdint.h>

#if defined(ARDUINO)
#include "../../../Protocol.h"
#include "../../../topics.h"
#endif

namespace CrabitArduboyAudio {

enum PairTerminator : uint8_t { TerminatorZero = 0, TerminatorHigh = 1 };

// API-source ladder: higher wins. Do not feed raw upstream priorities here.
enum Priority : uint8_t {
    PriorityScore = 1,     // background music / Playtune-compat score
    PrioritySequence = 2,  // ArduboyTones melody
    PriorityEffect = 3     // one-shot effect
};

enum PriorityDomain : uint8_t {
    DomainApi = 0,  // Priority ladder above
    DomainPort = 1  // upstream MyArduboy uint8_t: lower wins
};

// Contract limits. They mirror the STM32 receiver's parser in
// firmware/shared/game_tone.h; that file is not staged into this sketch, so the
// values are repeated here rather than adding a new build dependency.
constexpr uint16_t kMinFrequency = 100;
constexpr uint16_t kMaxFrequency = 5000;
constexpr uint16_t kMinDurationMs = 10;
constexpr uint16_t kMaxDurationMs = 250;
constexpr uint16_t kMinStartSpacingMs = 50; // 20 starts per second
constexpr uint16_t kRefreshMs = 200;        // before a >250 ms note expires
constexpr uint16_t kMaxTickSteps = 64;      // score commands parsed per tick
constexpr uint8_t kMaxPairsPerTick = 32;    // expired pair slots handled per tick
constexpr uint16_t kMaxScoreBytes = 2048;   // parse-work cap, not a memory bound
constexpr uint16_t kMaxPairEntries = 1024;  // parse-work cap, not a memory bound
constexpr uint8_t kMaxInlinePairs = 3;
constexpr uint32_t kInfiniteStep = 0x3FFFFFFF;

typedef void (*SinkFn)(void *context, uint16_t frequency, uint16_t durationMs);

namespace detail {

enum Kind : uint8_t { KindNone = 0, KindTone, KindInline, KindPairs, KindScore };

// A tone intent is small. Inline pairs are copied so the caller's stack never
// outlives the call; score/array pointers refer to corpus assets.
struct Intent {
    uint8_t kind = KindNone;
    uint8_t priority = PriorityEffect;
    uint8_t domain = DomainApi;
    uint8_t terminator = TerminatorHigh;
    uint8_t inlineCount = 0;
    int8_t pitch = 0;
    const uint8_t *score = nullptr;
    const uint16_t *pairs = nullptr;
    uint16_t inlinePairs[kMaxInlinePairs * 2] = {};
};

struct Voice {
    Intent intent;
    bool active = false;
    uint32_t nextStepAt = 0;  // absolute scheduled start of the next item
    uint16_t cursor = 0;      // Playtune byte offset
    uint16_t mark = 0;
    uint16_t repeats = 0;
    uint16_t pairIndex = 0;
    uint16_t noteFrequency = 0;
    uint32_t noteEndAt = 0;
    uint32_t refreshAt = 0;
    int8_t basePitch = 0;
    bool noteInfinite = false;
};

struct State {
    void *owner = nullptr;
    bool enabled = false;   // stays false until a live App mounts
    bool stopPending = false;
    bool hasPending = false;
    bool started = false;
    uint32_t lastStartAt = 0;
    Intent pending;
    Voice voice;
};

inline State &state() {
    static State instance;
    return instance;
}

inline uint16_t clampFrequency(uint16_t frequency) {
    if (frequency < kMinFrequency) return kMinFrequency;
    if (frequency > kMaxFrequency) return kMaxFrequency;
    return frequency;
}

inline uint16_t clampDuration(uint16_t durationMs) {
    if (durationMs < kMinDurationMs) return kMinDurationMs;
    if (durationMs > kMaxDurationMs) return kMaxDurationMs;
    return durationMs;
}

inline void formatTone(char *output, uint16_t frequency, uint16_t durationMs) {
    // Exact "frequency,duration_ms": no spaces, no sign, no JSON.
    char digits[6];
    uint8_t length = 0;
    do {
        digits[length++] = char('0' + (frequency % 10));
        frequency /= 10;
    } while (frequency);
    uint8_t at = 0;
    while (length) output[at++] = digits[--length];
    output[at++] = ',';
    length = 0;
    do {
        digits[length++] = char('0' + (durationMs % 10));
        durationMs /= 10;
    } while (durationMs);
    while (length) output[at++] = digits[--length];
    output[at] = '\0';
}

#if defined(ARDUINO)
inline void targetSend(void *, uint16_t frequency, uint16_t durationMs) {
    char payload[12];
    formatTone(payload, frequency, durationMs);
    (void)Protocol::send_message_checked(Topics::AppTone, payload, 0);
}
#endif

inline SinkFn &sinkFunction() {
    static SinkFn sink =
#if defined(ARDUINO)
        targetSend;
#else
        nullptr;
#endif
    return sink;
}

inline void *&sinkContext() {
    static void *context = nullptr;
    return context;
}

inline void emit(uint16_t frequency, uint16_t durationMs) {
    SinkFn sink = sinkFunction();
    if (!sink) return;
    sink(sinkContext(), clampFrequency(frequency), clampDuration(durationMs));
}

inline void emitStop() {
    SinkFn sink = sinkFunction();
    if (sink) sink(sinkContext(), 0, 0); // "0,0" is the literal stop contract.
}

inline void clearPending(State &s) {
    s.hasPending = false;
    s.pending = Intent();
}

inline void clearVoice(State &s) {
    s.voice = Voice();
}

// Stop now and forget the voice. Used for lifecycle/mute so no stale pointer
// can be dereferenced after the owning App State is gone.
inline void silence(State &s, bool send) {
    if (send) emitStop();
    clearPending(s);
    clearVoice(s);
}

inline bool due(uint32_t now, uint32_t when) {
    return int32_t(now - when) >= 0;
}

inline bool canStart(const State &s, uint32_t now) {
    return !s.started || uint32_t(now - s.lastStartAt) >= kMinStartSpacingMs;
}

inline void noteStarted(State &s, uint32_t now) {
    s.started = true;
    s.lastStartAt = now;
}

// Domain-aware comparison. Within one domain the upstream/compat rule applies;
// across domains a fixed precedence is used instead of comparing numbers from
// different scales.
inline bool outranks(const Intent &candidate, const Intent &current) {
    if (candidate.domain == current.domain) {
        if (candidate.domain == DomainPort)
            return candidate.priority <= current.priority; // lower wins; equal replaces
        return candidate.priority >= current.priority;     // higher wins; equal replaces
    }
    if (candidate.domain == DomainApi)
        return candidate.priority >= PrioritySequence; // effect/melody beats a port cue
    // A raw port cue may interrupt only a background API score, not an effect.
    return current.domain == DomainApi && current.priority <= PriorityScore;
}

// Convert a MIDI note number to Hz. Values below 100 Hz are clamped by the
// contract, matching the receiver's accepted range.
inline uint16_t midiFrequency(uint8_t note) {
    static const uint16_t table[128] = {
        8, 8, 9, 9, 10, 11, 11, 12, 13, 14, 14, 15, 16, 17, 18, 19,
        20, 22, 23, 24, 26, 27, 29, 31, 32, 34, 36, 39, 41, 43, 46, 49,
        52, 55, 58, 61, 65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 116, 123,
        131, 138, 147, 155, 165, 174, 185, 196, 207, 220, 233, 247, 261, 277,
        293, 311, 329, 349, 370, 392, 415, 440, 466, 494, 523, 554, 587,
        622, 659, 698, 740, 784, 830, 880, 932, 988, 1046, 1108, 1174,
        1244, 1318, 1397, 1480, 1568, 1661, 1760, 1864, 1975, 2093, 2217,
        2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951, 4186,
        4435, 4699, 4978, 5274, 5588, 5920, 6272, 6645, 7040, 7459, 7902,
        8372, 8870, 9397, 9956, 10548, 11175, 11840, 12544
    };
    return table[note & 0x7F];
}

inline bool registerIntent(State &s, const Intent &intent) {
    if (!s.owner || !s.enabled) return false;
    if (s.voice.active && !outranks(intent, s.voice.intent)) return false;
    if (s.hasPending && !outranks(intent, s.pending)) return false;
    s.pending = intent;
    s.hasPending = true;
    return true;
}

inline void adoptPending(State &s, uint32_t now) {
    if (!s.hasPending) return;
    if (s.voice.active && !outranks(s.pending, s.voice.intent)) {
        clearPending(s);
        return;
    }
    clearVoice(s);
    s.voice.intent = s.pending;
    s.voice.active = true;
    s.voice.cursor = 0;
    s.voice.mark = 0;
    s.voice.repeats = 0;
    s.voice.pairIndex = 0;
    s.voice.noteFrequency = 0;
    s.voice.noteInfinite = false;
    s.voice.basePitch = s.voice.intent.pitch;
    s.voice.nextStepAt = now;
    s.voice.refreshAt = now;
    clearPending(s);
}

inline void finishVoice(State &s) {
    clearVoice(s);
}

// One ArduboyTones pair or inline element. Returns false when the sequence
// ended (or the data is malformed). `nextStepAt` is an absolute sequence clock:
// expired or rate-limited notes are dropped without shifting later notes later
// than their scheduled time, so a dense sequence cannot stretch wall-clock.
inline bool stepPairs(State &s, const uint16_t *pairs, uint16_t &index, uint16_t count,
                      bool bounded, uint8_t terminator, uint32_t now,
                      bool retryOnBudget) {
    for (uint8_t guard = 0; guard < kMaxPairsPerTick; ++guard) {
        if (!due(now, s.voice.nextStepAt)) return true;
        if (bounded) {
            if (index + 1 >= count) return false;
        } else if (index + 1 >= kMaxPairEntries) {
            // Parse-work cap for an unterminated pointer. This is NOT an object
            // boundary check: it cannot prove the next two words are readable.
            return false;
        }
        uint16_t frequency = pairs[index];
        if (terminator == TerminatorZero) {
            if (frequency == 0) return false;
        } else if (frequency == 0x8000) {
            return false;
        } else if (frequency == 0x8001) {
            index = 0;
            s.voice.nextStepAt = now;
            continue;
        }
        // In the high-volume encoding bit 15 is not part of the frequency.
        frequency &= ~uint16_t(0x8000);
        const uint16_t duration = pairs[index + 1];

        // A (0,0) pair has no defined duration and would create an active but
        // silent voice forever; treat it as an explicit end/invalid marker.
        if (frequency == 0 && duration == 0) return false;

        const uint32_t start = s.voice.nextStepAt;
        if (duration == 0) {
            // A non-zero frequency with zero duration is upstream's continuous
            // tone. It parks the sequence clock but remains replaceable by a
            // stronger intent and stoppable by noTone()/stop().
            if (retryOnBudget && !canStart(s, now)) {
                s.voice.nextStepAt = s.lastStartAt + kMinStartSpacingMs;
                return true;
            }
            index = uint16_t(index + 2);
            s.voice.noteFrequency = frequency;
            s.voice.noteInfinite = true;
            s.voice.noteEndAt = 0;
            s.voice.refreshAt = now + kRefreshMs;
            s.voice.nextStepAt = start + kInfiniteStep;
            emit(frequency, kMaxDurationMs);
            noteStarted(s, now);
            return true;
        }

        const uint32_t end = start + duration;
        s.voice.nextStepAt = end;
        index = uint16_t(index + 2);

        if (frequency == 0) {
            // Musical rest: no wire request, but it still occupies timeline.
            s.voice.noteFrequency = 0;
            s.voice.noteInfinite = false;
            continue;
        }

        if (retryOnBudget) {
            // One-shot/inline tone: keep its slot until the start budget opens.
            if (!canStart(s, now)) {
                index = uint16_t(index - 2);
                s.voice.nextStepAt = s.lastStartAt + kMinStartSpacingMs;
                return true;
            }
            s.voice.noteFrequency = frequency;
            s.voice.noteInfinite = false;
            s.voice.noteEndAt = now + duration;
            s.voice.refreshAt = duration > kMaxDurationMs ? now + kRefreshMs
                                                          : now + duration;
            emit(frequency, duration);
            noteStarted(s, now);
            return true;
        }

        // Running sequence: preserve the absolute timeline. A note whose slot
        // has already fully elapsed is dropped; a live 1..9 ms note is sampled
        // and emit() clamps its wire duration up to the 10 ms contract. The
        // 20 starts/s budget drops notes without pushing later ones back.
        const int32_t remaining = int32_t(end - now);
        if (remaining <= 0) continue;
        if (!canStart(s, now)) continue;

        s.voice.noteFrequency = frequency;
        s.voice.noteInfinite = false;
        s.voice.noteEndAt = end;
        s.voice.refreshAt =
            uint32_t(remaining) > kMaxDurationMs ? now + kRefreshMs : end;
        emit(frequency, uint16_t(remaining > kMaxDurationMs ? kMaxDurationMs
                                                            : remaining));
        noteStarted(s, now);
        return true;
    }
    return true; // per-tick work cap; remaining due pairs continue next tick
}

// Playtune opcodes verified against the pinned OBONO MyArduboyPlaytune.cpp:
//   0x80 stop note (mono: implicitly timed by the previous wait)
//   0x90 play note + note byte
//   <0x80 15-bit big-endian wait in ms (the note's sounding time)
//   0xD0 mark/repeat point (low nibble transposes on later repeats)
//   0xE0 repeat 2^n times (n == 0 repeats forever)
//   0xF0 stop
// Unknown opcodes and a zero wait are treated as invalid and stop the voice
// instead of spinning forever.
inline bool stepScore(State &s, uint32_t now) {
    static const int8_t transpose[16] = {
        0, 1, 2, 3, 4, 6, 8, 12, 0, -12, -8, -6, -4, -3, -2, -1
    };
    const uint8_t *score = s.voice.intent.score;
    if (!score) return false;
    uint16_t chosen = 0;
    for (uint16_t step = 0; step < kMaxTickSteps; ++step) {
        if (s.voice.cursor >= kMaxScoreBytes) return false;
        const uint8_t command = score[s.voice.cursor++];
        const uint8_t opcode = command & 0xF0;
        const uint8_t value = command & 0x0F;
        if (opcode == 0x80) {
            continue; // STOPNOTE: the mono note already has a bounded duration
        }
        if (opcode == 0x90) {
            if (s.voice.cursor >= kMaxScoreBytes) return false;
            int16_t note = int16_t(score[s.voice.cursor++]) + s.voice.intent.pitch;
            if (note < 0) note = 0;
            if (note > 127) note = 127;
            chosen = midiFrequency(uint8_t(note));
            continue;
        }
        if (command < 0x80) {
            if (s.voice.cursor >= kMaxScoreBytes) return false;
            const uint16_t wait = uint16_t(command) << 8 | score[s.voice.cursor++];
            if (wait == 0) return false;
            if (chosen != 0) {
                if (canStart(s, now)) {
                    emit(chosen, wait);
                    noteStarted(s, now);
                    s.voice.noteFrequency = chosen;
                    s.voice.noteInfinite = false;
                    s.voice.noteEndAt = now + wait;
                    s.voice.refreshAt =
                        wait > kMaxDurationMs ? now + kRefreshMs : now + wait;
                } else {
                    s.voice.noteFrequency = 0;
                    s.voice.noteInfinite = false;
                }
            } else {
                s.voice.noteFrequency = 0;
                s.voice.noteInfinite = false;
            }
            s.voice.nextStepAt = now + wait;
            return true;
        }
        if (opcode == 0xD0) {
            s.voice.mark = uint16_t(s.voice.cursor - 1);
            if (s.voice.repeats > 0)
                s.voice.intent.pitch = int8_t(s.voice.intent.pitch + transpose[value]);
            continue;
        }
        if (opcode == 0xE0) {
            if (value == 0 || ++s.voice.repeats < (uint16_t(1) << value)) {
                s.voice.cursor = s.voice.mark;
                continue;
            }
            s.voice.mark = s.voice.cursor;
            s.voice.repeats = 0;
            s.voice.intent.pitch = s.voice.basePitch;
            return true;
        }
        if (opcode == 0xF0) return false;
        return false; // Unknown opcode: fail closed.
    }
    return false; // Step budget exhausted: malformed no-wait loop.
}

inline void refreshLongNote(State &s, uint32_t now) {
    if (!s.voice.noteFrequency) return;
    if (!due(now, s.voice.refreshAt)) return;
    if (!canStart(s, now)) return;
    if (s.voice.noteInfinite) {
        emit(s.voice.noteFrequency, kMaxDurationMs);
        noteStarted(s, now);
        s.voice.refreshAt = now + kRefreshMs;
        return;
    }
    if (due(now, s.voice.noteEndAt)) return; // note already finished
    const uint32_t remaining = s.voice.noteEndAt - now;
    emit(s.voice.noteFrequency, remaining > kMaxDurationMs ? kMaxDurationMs
                                                          : uint16_t(remaining));
    noteStarted(s, now);
    s.voice.refreshAt = now + kRefreshMs;
}

} // namespace detail

// Host tests: capture emitted pairs instead of the real Protocol transport.
inline void setSink(SinkFn sink, void *context = nullptr) {
    detail::sinkFunction() = sink;
    detail::sinkContext() = context;
}

// Restore the platform default (real Protocol send on the target, none on host).
inline void resetSink() {
#if defined(ARDUINO)
    detail::sinkFunction() = detail::targetSend;
#else
    detail::sinkFunction() = nullptr;
#endif
    detail::sinkContext() = nullptr;
}

// Claim audio for one live foreground App. Mounting a different owner stops a
// still-playing previous owner first. resetEnabled distinguishes a fresh mount
// (per-game default on) from a resume (the App restores its own saved choice).
inline void begin(void *owner, bool resetEnabled = true) {
    detail::State &s = detail::state();
    if (s.owner != owner &&
        (s.voice.active || s.hasPending || s.stopPending))
        detail::silence(s, true);
    detail::clearPending(s);
    detail::clearVoice(s);
    s.stopPending = false;
    s.owner = owner;
    s.started = false;
    if (resetEnabled) s.enabled = true;
}

// Release a suspended/unmounted owner. Only the current owner can stop audio,
// so a replacement game can never be muted by a stale App pointer.
inline void end(void *owner) {
    detail::State &s = detail::state();
    if (s.owner != owner) return;
    detail::silence(s, s.voice.active || s.hasPending || s.stopPending ||
                           s.voice.noteFrequency != 0);
    s.owner = nullptr;
    s.stopPending = false;
}

inline bool hasOwner() { return detail::state().owner != nullptr; }
inline void *owner() { return detail::state().owner; }
inline bool enabled() { return detail::state().enabled; }

inline void setEnabled(bool on) {
    detail::State &s = detail::state();
    if (s.enabled == on) return;
    s.enabled = on;
    if (!on && s.owner) {
        // Mute is synchronous with the user's menu action; the wire stop still
        // happens in tick() so it keeps the stop-bypasses-budget rule. Queue it
        // only if something was started or is still queued: restoring a muted
        // App (or muting before any sound) must not leave a redundant stop that
        // could arrive next to a fresh intent.
        if (s.started || s.voice.active || s.hasPending ||
            s.voice.noteFrequency != 0) {
            s.stopPending = true;
        }
        detail::clearPending(s);
    } else if (on) {
        // Re-enabling before the stop tick means a fresh sound replaces the old
        // one; do not let a stale stop swallow the new intent.
        s.stopPending = false;
    }
}

// Registration only: gameplay stack never formats or writes serial data.
// `priority` is the API ladder. A (0,0) request is an explicit no-op:
// it never creates an active silent voice.
inline void playTone(uint16_t frequency, uint16_t durationMs,
                     uint8_t priority = PriorityEffect) {
    if (frequency == 0) return; // standalone rest: nothing to schedule
    detail::State &s = detail::state();
    detail::Intent intent;
    intent.kind = detail::KindTone;
    intent.priority = priority;
    intent.domain = DomainApi;
    intent.inlinePairs[0] = frequency;
    intent.inlinePairs[1] = durationMs;
    intent.inlineCount = 1;
    detail::registerIntent(s, intent);
}

// Raw upstream MyArduboy priority (lower wins), used by the port wrappers that
// still expose the original playTone(priority) API. Kept separate from the API
// ladder above.
inline void playToneRaw(uint16_t frequency, uint16_t durationMs,
                        uint8_t rawPriority) {
    if (frequency == 0) return;
    detail::State &s = detail::state();
    detail::Intent intent;
    intent.kind = detail::KindTone;
    intent.priority = rawPriority;
    intent.domain = DomainPort;
    intent.inlinePairs[0] = frequency;
    intent.inlinePairs[1] = durationMs;
    intent.inlineCount = 1;
    detail::registerIntent(s, intent);
}

// Fixed multi-tone overload (ArduboyTones::tone with 2 or 3 pairs). The pairs
// are copied into the intent, so no caller stack pointer outlives the call.
// The name is distinct so playTone(0, 0) can never resolve to a pointer.
inline void playTonePairs(const uint16_t *pairs, uint8_t pairCount,
                          uint8_t priority = PriorityEffect) {
    if (!pairs || pairCount == 0) return;
    if (pairCount > kMaxInlinePairs) pairCount = kMaxInlinePairs;
    detail::State &s = detail::state();
    detail::Intent intent;
    intent.kind = detail::KindInline;
    intent.priority = priority;
    intent.domain = DomainApi;
    intent.inlineCount = pairCount;
    for (uint8_t i = 0; i < uint8_t(pairCount * 2); ++i)
        intent.inlinePairs[i] = pairs[i];
    detail::registerIntent(s, intent);
}

// Caller-owned sequence: the array must stay alive and be terminated while it
// plays. kMaxPairEntries only limits parse work for a non-terminated pointer.
inline void playTones(const uint16_t *pairs, uint8_t terminator = TerminatorHigh,
                      uint8_t priority = PrioritySequence) {
    if (!pairs) return;
    detail::State &s = detail::state();
    detail::Intent intent;
    intent.kind = detail::KindPairs;
    intent.priority = priority;
    intent.domain = DomainApi;
    intent.terminator = terminator;
    intent.pairs = pairs;
    detail::registerIntent(s, intent);
}

inline void playPlaytune(const uint8_t *score, uint8_t priority = PriorityScore,
                         int8_t pitch = 0) {
    if (!score) return;
    detail::State &s = detail::state();
    detail::Intent intent;
    intent.kind = detail::KindScore;
    intent.priority = priority;
    intent.domain = DomainApi;
    intent.pitch = pitch;
    intent.score = score;
    detail::registerIntent(s, intent);
}

// Upstream MyArduboy playScore2(): lower raw priority is more important and is
// shared with playToneRaw within the same port.
inline void playPlaytuneRaw(const uint8_t *score, uint8_t rawPriority,
                            int8_t pitch = 0) {
    if (!score) return;
    detail::State &s = detail::state();
    detail::Intent intent;
    intent.kind = detail::KindScore;
    intent.priority = rawPriority;
    intent.domain = DomainPort;
    intent.pitch = pitch;
    intent.score = score;
    detail::registerIntent(s, intent);
}

// Stop registers intent; tick() emits "0,0" and bypasses the start budget.
inline void stop() {
    detail::State &s = detail::state();
    if (!s.owner) return;
    s.stopPending = true;
    detail::clearPending(s);
    detail::clearVoice(s);
}

inline bool playing() {
    const detail::State &s = detail::state();
    return s.owner != nullptr && s.enabled && (s.voice.active || s.hasPending);
}

// Bounded audio tick: called once per ArduboyCompatApp::onUpdate. Never blocks,
// never allocates, and never dereferences a sequence after the owner is gone.
inline void tick(uint32_t nowMs) {
    detail::State &s = detail::state();
    if (!s.owner) {
        if (s.voice.active || s.hasPending) detail::silence(s, false);
        s.stopPending = false;
        return;
    }
    if (s.stopPending) {
        // Emit this tick, but keep anything registered after stop(); it is
        // adopted on the next tick, so at most one wire request leaves here.
        detail::emitStop();
        s.stopPending = false;
        detail::clearVoice(s);
        return;
    }
    if (!s.enabled) {
        if (s.voice.active || s.hasPending) detail::silence(s, true);
        return;
    }
    detail::adoptPending(s, nowMs);
    if (!s.voice.active) return;

    detail::refreshLongNote(s, nowMs);
    if (!detail::due(nowMs, s.voice.nextStepAt)) return;

    switch (s.voice.intent.kind) {
    case detail::KindTone:
    case detail::KindInline: {
        const uint16_t *pairs = s.voice.intent.inlinePairs;
        const uint16_t count = uint16_t(s.voice.intent.inlineCount * 2);
        if (!detail::stepPairs(s, pairs, s.voice.pairIndex, count, true,
                               TerminatorHigh, nowMs, true))
            detail::finishVoice(s);
        break;
    }
    case detail::KindPairs:
        if (!detail::stepPairs(s, s.voice.intent.pairs, s.voice.pairIndex,
                               kMaxPairEntries, false,
                               s.voice.intent.terminator, nowMs, false))
            detail::finishVoice(s);
        break;
    case detail::KindScore:
        if (!detail::stepScore(s, nowMs)) detail::finishVoice(s);
        break;
    default:
        detail::finishVoice(s);
        break;
    }
}

} // namespace CrabitArduboyAudio

#endif // CRABIT_ARDUBOY_AUDIO_COMPAT_H
