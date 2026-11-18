// Host tests for the real Arduboy audio compatibility adapter.
//
// Only the STM32 wire sink is replaced by a capture function; the sequence
// parsers, priority domains, rate budget, owner/mute rules and Arduboy2Compat
// facade are the production headers. No Protocol, SDK, device or embedded
// build is involved.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Real shipped Arduboy3D cue data. The game calls
// Platform::PlaySound() -> sound.tones(pattern), so including the actual
// source arrays keeps this regression tied to the corpus instead of a copy.
// Sounds.cpp's TONES_END macro would shadow the compat enum below.
#include "../../../firmware/rtl8720dn/NanoUI/app/apps/games/arduboy3d/Sounds.cpp"
#undef TONES_END

#include "app/compat/arduboy/Arduboy2Compat.h"

namespace audio = CrabitArduboyAudio;

namespace {

struct Capture {
    uint16_t frequency[160];
    uint16_t duration[160];
    uint32_t at[160];
    unsigned count = 0;
    void reset() { count = 0; }
    bool lastIsStop() const {
        return count && frequency[count - 1] == 0 && duration[count - 1] == 0;
    }
};

Capture captured;
uint32_t g_now = 0;

void capture(void *, uint16_t frequency, uint16_t duration) {
    if (captured.count < 160) {
        captured.frequency[captured.count] = frequency;
        captured.duration[captured.count] = duration;
        captured.at[captured.count] = g_now;
        ++captured.count;
    }
}

void tickAt(uint32_t now) {
    g_now = now;
    audio::tick(now);
}

void resetWorld() {
    audio::end(audio::owner());
    audio::setSink(capture);
    captured.reset();
    g_now = 0;
}

void testRegistrationAndSimpleTone() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    audio::playTone(440, 100);
    assert(captured.count == 0); // Gameplay calls only register intent.
    assert(audio::playing());
    tickAt(0);
    assert(captured.count == 1);
    assert(captured.frequency[0] == 440 && captured.duration[0] == 100);
    assert(audio::playing());
    tickAt(100);
    assert(captured.count == 1); // Note expired; no extra wire stop needed.
    assert(!audio::playing());
    audio::end(&owner);
}

void testBoundsAndHighVolumeBit() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    audio::playTone(50, 5, audio::PriorityEffect);
    tickAt(0);
    assert(captured.count == 1);
    assert(captured.frequency[0] == audio::kMinFrequency);
    assert(captured.duration[0] == audio::kMinDurationMs);
    audio::playTone(9000, 1000, audio::PriorityEffect);
    tickAt(1000);
    assert(captured.count == 2);
    assert(captured.frequency[1] == audio::kMaxFrequency);
    assert(captured.duration[1] == audio::kMaxDurationMs);
    // NOTE_CS3H carries ArduboyTones' high-volume bit; it must not become the
    // wire frequency.
    audio::end(&owner);
    audio::begin(&owner);
    captured.reset();
    audio::playTone(NOTE_CS3H, 30, audio::PriorityEffect);
    tickAt(2000);
    assert(captured.count == 1);
    assert(captured.frequency[0] == NOTE_CS3);
    audio::end(&owner);
}

void testCoalescingAndRateBudget() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    uint32_t now = 0;
    for (unsigned i = 0; i < 40; ++i) {
        audio::playTone(uint16_t(400 + i), 10, audio::PriorityEffect);
        now += 10;
        tickAt(now);
    }
    // 400 ms of spam must not exceed the 20 starts/s contract.
    assert(captured.count <= 9);
    for (unsigned i = 1; i < captured.count; ++i)
        assert(captured.at[i] - captured.at[i - 1] >= audio::kMinStartSpacingMs);
    // The latest intent is not lost: the last admitted note is still 439's slot.
    tickAt(now + 100);
    assert(captured.frequency[captured.count - 1] == 439);
    audio::end(&owner);
}

void testStopBypassesBudget() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    audio::playTone(440, 250, audio::PriorityEffect);
    tickAt(0);
    audio::playTone(450, 250, audio::PriorityEffect);
    tickAt(10); // Still inside the 50 ms start budget.
    assert(captured.count == 1);
    audio::stop();
    assert(!audio::playing());
    tickAt(20); // Stop must ignore the start budget.
    assert(captured.count == 2 && captured.lastIsStop());
    tickAt(30);
    assert(captured.count == 2);
    audio::end(&owner);

    // stop() followed by unmount before the next tick must still put a stop on
    // the wire; stop() cleared the voice, so end() checks stopPending too.
    resetWorld();
    audio::begin(&owner);
    audio::playTone(440, 250, audio::PriorityEffect);
    tickAt(0);
    audio::stop();
    audio::end(&owner);
    assert(captured.lastIsStop());
}

void testStopPreservesNewIntent() {
    // stop() then a new intent: the stop leaves this tick, the new sound is
    // adopted on the next tick (never both on one tick).
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    audio::stop();
    audio::playTone(600, 100, audio::PriorityEffect);
    assert(audio::playing());
    tickAt(0);
    assert(captured.count == 1 && captured.lastIsStop());
    assert(audio::playing()); // pending survived the stop tick
    tickAt(20);
    assert(captured.count == 2);
    assert(captured.frequency[1] == 600 && captured.duration[1] == 100);
    audio::end(&owner);

    // play() then stop() in one frame: stop cancels the new intent.
    resetWorld();
    audio::begin(&owner);
    audio::playTone(700, 100, audio::PriorityEffect);
    audio::stop();
    tickAt(0);
    assert(captured.count == 1 && captured.lastIsStop());
    tickAt(100);
    assert(captured.count == 1 && !audio::playing());
    audio::end(&owner);

    // Same ordering for a score pointer.
    resetWorld();
    static const uint8_t score[] = {0x90, 69, 0, 40, 0x80, 0xF0};
    audio::begin(&owner);
    audio::stop();
    audio::playPlaytune(score);
    tickAt(0);
    assert(captured.count == 1 && captured.lastIsStop());
    tickAt(20);
    assert(captured.count == 2 && captured.frequency[1] == 440);
    audio::end(&owner);
}

void testMuteAndReenable() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    audio::playTone(440, 100, audio::PriorityEffect);
    tickAt(0);
    audio::setEnabled(false);
    assert(!audio::enabled());
    audio::playTone(500, 100, audio::PriorityEffect);
    tickAt(20);
    assert(captured.lastIsStop());
    assert(!audio::playing());
    const unsigned afterMute = captured.count;
    tickAt(100);
    assert(captured.count == afterMute); // no repeated stop spam while muted
    audio::setEnabled(true);
    audio::playTone(500, 100, audio::PriorityEffect);
    tickAt(200);
    assert(captured.frequency[captured.count - 1] == 500);
    audio::end(&owner);

    // Restore path (begin(owner, false) + saved mute) with nothing playing must
    // not queue a redundant stop, and the next real sound must still go out.
    resetWorld();
    audio::begin(&owner, false);
    audio::setEnabled(false);
    tickAt(0);
    assert(captured.count == 0);
    audio::playTone(600, 100, audio::PriorityEffect); // muted: rejected
    tickAt(20);
    assert(captured.count == 0);
    audio::setEnabled(true);
    audio::playTone(700, 100, audio::PriorityEffect);
    tickAt(50);
    assert(captured.count == 1 && captured.frequency[0] == 700);
    audio::end(&owner);

    // Muting a sounding voice must still stop it (stopPending preserved).
    resetWorld();
    audio::begin(&owner);
    audio::playTone(440, 250, audio::PriorityEffect);
    tickAt(0);
    audio::setEnabled(false);
    tickAt(20);
    assert(captured.count == 2 && captured.lastIsStop());
    audio::end(&owner);
}

void testOwnerLifetimeAndStalePointer() {
    resetWorld();
    int ownerA = 0, ownerB = 0;
    static const uint8_t score[] = {0x90, 69, 0x01, 0xF4, 0x80, 0xF0};
    audio::begin(&ownerA);
    audio::playPlaytune(score, audio::PriorityScore);
    tickAt(0);
    assert(captured.count == 1);
    audio::end(&ownerB); // A stale pointer cannot stop the live owner.
    assert(audio::playing());
    tickAt(100);
    assert(audio::playing());
    audio::end(&ownerA); // Lifecycle stop, immediate and pointer-free.
    assert(captured.lastIsStop());
    assert(!audio::playing());
    const unsigned afterEnd = captured.count;
    audio::playTone(440, 100, audio::PriorityEffect);
    tickAt(300);
    tickAt(400);
    assert(captured.count == afterEnd); // No audio while unowned.
    audio::begin(&ownerB);
    assert(!audio::playing());
    audio::end(&ownerB);
}

void testTonesHighTerminatorAndRepeat() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    static const uint16_t melody[] = {262, 60, 330, 40, 0x8000};
    audio::playTones(melody);
    tickAt(0);
    assert(captured.count == 1 && captured.frequency[0] == 262);
    tickAt(60);
    assert(captured.count == 2 && captured.frequency[1] == 330);
    tickAt(100);
    assert(!audio::playing());

    captured.reset();
    static const uint16_t loop[] = {392, 60, 0x8001};
    audio::playTones(loop);
    tickAt(200);
    tickAt(260);
    tickAt(320);
    assert(captured.count == 3); // repeat restarts at the first pair
    assert(captured.frequency[0] == 392 && captured.frequency[2] == 392);
    audio::stop();
    tickAt(330);
    assert(captured.lastIsStop() && !audio::playing());
    audio::end(&owner);
}

void testTonesZeroTerminator() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    static const uint16_t zeroEnded[] = {500, 80, 250, 130, 0};
    audio::playTones(zeroEnded, audio::TerminatorZero);
    tickAt(0);
    assert(captured.count == 1 && captured.frequency[0] == 500);
    tickAt(80);
    assert(captured.count == 2 && captured.frequency[1] == 250);
    tickAt(210);
    assert(!audio::playing()); // 0 is this port's explicit end marker.
    audio::end(&owner);
}

void testDenseSequenceKeepsWallclock() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    static uint16_t dense[65];
    for (unsigned i = 0; i < 32; ++i) {
        dense[i * 2] = uint16_t(400 + i);
        dense[i * 2 + 1] = 7; // 7 ms notes, far denser than 20 starts/s
    }
    dense[64] = TONES_END; // the array contract needs a terminator
    audio::playTones(dense);
    unsigned ticks = 0;
    for (uint32_t now = 0; now <= 240; now += 16) {
        tickAt(now);
        ++ticks;
    }
    // 32 * 7 ms = 224 ms of score. The old code stretched the queue to ~1 s.
    // Sub-10 ms notes are now sampled live and emit() clamps them to the wire
    // minimum, so a few cues survive while the wall-clock end stays ~224 ms.
    assert(!audio::playing());
    assert(captured.count >= 1 && captured.count <= 5);
    assert(captured.count <= ticks); // never more than one wire request per tick
    for (unsigned i = 1; i < captured.count; ++i)
        assert(captured.at[i] - captured.at[i - 1] >= audio::kMinStartSpacingMs);
    for (unsigned i = 0; i < captured.count; ++i) {
        assert(captured.at[i] <= 240); // no start dragged past the sequence end
        assert(captured.duration[i] >= audio::kMinDurationMs);
        assert(captured.duration[i] <= audio::kMaxDurationMs);
    }

    // A 20 ms melody is playable but still denser than 20 starts/s: some notes
    // are coalesced while the 320 ms timeline is preserved.
    audio::end(&owner);
    audio::begin(&owner);
    captured.reset();
    static uint16_t dense20[33];
    for (unsigned i = 0; i < 16; ++i) {
        dense20[i * 2] = uint16_t(500 + i);
        dense20[i * 2 + 1] = 20;
    }
    dense20[32] = TONES_END;
    audio::playTones(dense20);
    ticks = 0;
    for (uint32_t now = 0; now <= 336; now += 16) {
        tickAt(now);
        ++ticks;
    }
    assert(!audio::playing());
    assert(captured.count >= 1 && captured.count <= 6);
    assert(captured.count <= ticks);
    for (unsigned i = 1; i < captured.count; ++i)
        assert(captured.at[i] - captured.at[i - 1] >= audio::kMinStartSpacingMs);
    for (unsigned i = 0; i < captured.count; ++i) {
        assert(captured.duration[i] >= audio::kMinDurationMs);
        assert(captured.duration[i] <= audio::kMaxDurationMs);
    }
    audio::end(&owner);
}

void testPlaytuneNotesWaitAndStop() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    static const uint8_t score[] = {0x90, 69, 0, 100, 0x90, 72, 0, 40, 0x80, 0xF0};
    audio::playPlaytune(score);
    tickAt(0);
    assert(captured.count == 1 && captured.frequency[0] == 440);
    assert(captured.duration[0] == 100);
    tickAt(100);
    assert(captured.count == 2 && captured.frequency[1] == 523);
    tickAt(140);
    assert(!audio::playing()); // 0x80 STOPNOTE + 0xF0 STOP.
    audio::end(&owner);
}

void testPlaytuneMarkRepeatTranspose() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    static const uint8_t score[] = {
        0xD2,                    // mark; table[2] transposes +2 each repeat
        0x90, 69, 0, 60,         // note 69 for 60 ms
        0xE2,                    // repeat 4 times
        0x80, 0xF0
    };
    audio::playPlaytune(score);
    uint32_t now = 0;
    for (unsigned i = 0; i < 8; ++i) {
        tickAt(now);
        now += 60;
    }
    assert(captured.count == 4);
    assert(captured.frequency[0] == 440);
    assert(captured.frequency[1] == 494);
    assert(captured.frequency[2] == 554);
    assert(captured.frequency[3] == 622);
    assert(!audio::playing());
    audio::end(&owner);
}

void testPlaytuneInfiniteRepeatAndStop() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    static const uint8_t score[] = {0xD0, 0x90, 69, 0x01, 0x2C, 0xE0, 0xF0};
    audio::playPlaytune(score);
    for (uint32_t now = 0; now <= 1200; now += 100) tickAt(now);
    assert(audio::playing());
    audio::stop();
    tickAt(1300);
    assert(captured.lastIsStop() && !audio::playing());
    audio::end(&owner);
}

void testInvalidAndWorkCaps() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);

    static const uint8_t zeroWait[] = {0x90, 69, 0, 0};
    audio::playPlaytune(zeroWait);
    tickAt(0);
    assert(!audio::playing()); // A zero wait is invalid, not an infinite step.

    static const uint8_t unknown[] = {0xA0, 0xF0};
    audio::playPlaytune(unknown);
    tickAt(100);
    assert(!audio::playing()); // Unknown opcode fails closed.

    uint8_t noWait[64];
    memset(noWait, 0x80, sizeof(noWait)); // STOPNOTE forever, no wait opcode.
    audio::playPlaytune(noWait);
    tickAt(200);
    assert(!audio::playing()); // Per-tick step budget stops the malformed loop.

    uint8_t huge[audio::kMaxScoreBytes];
    memset(huge, 0x80, sizeof(huge));
    audio::playPlaytune(huge);
    tickAt(300);
    assert(!audio::playing());

    // A pointer that is not terminated is outside the memory contract; the
    // parse-work cap still stops it after at most kMaxPairEntries words.
    static uint16_t pairs[audio::kMaxPairEntries];
    for (unsigned i = 0; i < audio::kMaxPairEntries; i += 2) {
        pairs[i] = 440;
        pairs[i + 1] = 10;
    }
    audio::playTones(pairs);
    for (uint32_t now = 400; now < 32000; now += 50) tickAt(now);
    assert(!audio::playing());
    audio::end(&owner);
}

void testLongNoteRefresh() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    audio::playTone(440, 1000, audio::PriorityEffect);
    for (uint32_t now = 0; now <= 1000; now += 50) tickAt(now);
    assert(captured.count >= 4);
    for (unsigned i = 0; i < captured.count; ++i) {
        assert(captured.frequency[i] == 440);
        assert(captured.duration[i] <= audio::kMaxDurationMs);
    }
    assert(!audio::playing());
    audio::end(&owner);
}

void testRawPortPriorityOrdering() {
    // Pinned OBONO MyArduboy: a LOWER uint8_t is more important. These are the
    // real lasers/chribocchi/hopper values (START 0, HIT 2, LASER 4, click 255).
    static const uint8_t start[] = {0x90, 81, 0x01, 0x2C, 0x80, 0xF0}; // A5
    static const uint8_t hit[] = {0x90, 69, 0x01, 0x2C, 0x80, 0xF0};   // A4
    static const uint8_t click[] = {0x90, 72, 0x00, 0x40, 0x80, 0xF0}; // C5
    static const uint8_t laser[] = {0x90, 74, 0x00, 0x40, 0x80, 0xF0}; // D5

    resetWorld();
    Arduboy2Compat engine;
    audio::begin(&engine);
    engine.playScore2(hit, 2); // raw HIT
    tickAt(0);
    assert(captured.count == 1 && captured.frequency[0] == 440);

    // START (0) outranks HIT (2) and replaces it.
    engine.playScore2(start, 0);
    tickAt(60);
    assert(captured.count == 2 && captured.frequency[1] == 880);

    // click (255) must not monopolize a higher-priority START sound.
    engine.playScore2(click, 255);
    tickAt(120);
    assert(captured.count == 2);

    // Let START finish, then a click is the only voice.
    tickAt(400);
    engine.playScore2(click, 255);
    tickAt(410);
    assert(captured.frequency[captured.count - 1] == 523);

    // LASER (4) outranks click (255) and replaces it.
    engine.playScore2(laser, 4);
    tickAt(460);
    assert(captured.frequency[captured.count - 1] == 587);

    // An API one-shot effect outranks a raw port cue across domains.
    audio::playTone(1000, 40, audio::PriorityEffect);
    tickAt(520);
    assert(captured.frequency[captured.count - 1] == 1000);

    // A raw cue may replace an API background score, not an effect.
    resetWorld();
    static const uint8_t score[] = {0x90, 69, 0x01, 0x2C, 0x80, 0xF0};
    audio::begin(&engine);
    audio::playPlaytune(score, audio::PriorityScore);
    tickAt(0);
    audio::playToneRaw(660, 40, 255);
    tickAt(60);
    assert(captured.frequency[captured.count - 1] == 660);
    audio::end(&engine);
}

void testZeroDurationAndPointerAmbiguity() {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);

    // playTone(0, 0) is now unambiguous and is an explicit no-op, never an
    // active silent voice that blocks later audio.
    audio::playTone(0, 0);
    assert(!audio::playing());
    tickAt(0);
    assert(captured.count == 0);

    // A non-zero frequency with duration 0 is upstream's continuous tone.
    audio::playTone(440, 0);
    tickAt(10);
    assert(captured.count == 1 && captured.frequency[0] == 440);
    assert(captured.duration[0] == audio::kMaxDurationMs);
    tickAt(300);
    assert(captured.count == 2 && captured.frequency[1] == 440);

    // A (0,0) pair terminates instead of parking a silent infinite voice.
    audio::stop();
    tickAt(310);
    const unsigned afterStop = captured.count;
    static const uint16_t zeroPair[] = {0, 0};
    audio::playTones(zeroPair);
    tickAt(400);
    assert(!audio::playing());
    assert(captured.count == afterStop);

    // A fixed pair list with a valid first tone still plays.
    static const uint16_t pairList[] = {500, 30};
    audio::playTonePairs(pairList, 1);
    tickAt(500);
    assert(captured.frequency[captured.count - 1] == 500);
    audio::end(&owner);
}

void testFacadeAndNoteConstants() {
    static_assert(NOTE_A4 == 440, "NOTE_A4 must restore upstream pitch");
    static_assert((NOTE_CS3H & 0x7FFF) == NOTE_CS3, "CS3H is the volume bit");
    static_assert(TONES_END == 0x8000, "high terminator");
    static_assert(TONES_REPEAT == 0x8001, "repeat marker");

    resetWorld();
    Arduboy2Compat engine;
    audio::begin(&engine);
    engine.audio.on();
    assert(engine.audio.enabled() && engine.isAudioEnabled());
    engine.audio.off();
    assert(!engine.audio.enabled() && !engine.isAudioEnabled());
    engine.audio.on();

    static const uint8_t score[] = {0x90, 69, 0, 40, 0x80, 0xF0};
    engine.playScore2(score, 0);
    tickAt(0);
    assert(captured.count == 1 && captured.frequency[0] == 440);
    engine.tone2(330, 40);
    tickAt(50);
    assert(captured.frequency[captured.count - 1] == 330);
    engine.stopScore2();
    tickAt(100);
    assert(captured.lastIsStop());
    audio::end(&engine);
}

// Arduboy3D's shipped cues enter through the same ArduboyTonesCompat::tones()
// path as Platform::PlaySound: a fixed array with the 0x8000 terminator.
uint32_t corpusTimelineMs(const uint16_t *pairs) {
    uint32_t total = 0;
    for (uint16_t index = 0; index + 1 < audio::kMaxPairEntries; index += 2) {
        if (pairs[index] == TONES_END) return total;
        total += pairs[index + 1];
    }
    assert(false && "corpus cue is not terminated");
    return 0;
}

void checkCorpusCue(const uint16_t *pairs, const char *name) {
    resetWorld();
    int owner = 0;
    audio::begin(&owner);
    const uint32_t total = corpusTimelineMs(pairs);
    assert(total > 0);
    audio::playTones(pairs);
    assert(audio::playing());

    uint32_t endAt = 0;
    unsigned ticks = 0;
    for (uint32_t now = 0; now <= total + 64; now += 16) {
        tickAt(now);
        ++ticks;
        if (!audio::playing()) {
            endAt = now;
            break;
        }
    }

    assert(!audio::playing());
    // The absolute sequence clock ends at the summed pair durations; tick
    // granularity may add at most one 16 ms frame.
    assert(endAt >= total && endAt <= total + 16);
    assert(captured.count >= 1); // sub-10 ms cues are sampled, not silent
    assert(captured.count <= total / audio::kMinStartSpacingMs + 1);
    assert(captured.count <= ticks); // at most one wire request per tick
    for (unsigned i = 1; i < captured.count; ++i)
        assert(captured.at[i] - captured.at[i - 1] >= audio::kMinStartSpacingMs);
    for (unsigned i = 0; i < captured.count; ++i) {
        assert(captured.at[i] <= total);
        assert(captured.frequency[i] != 0);
        assert(captured.duration[i] >= audio::kMinDurationMs);
        assert(captured.duration[i] <= audio::kMaxDurationMs);
    }
    printf("corpus %s: %u cues over %u ms\n", name, captured.count, total);
    audio::end(&owner);
}

void testRealArduboy3dCorpusCues() {
    checkCorpusCue(Sounds::Hit, "Hit");
    checkCorpusCue(Sounds::PlayerDeath, "PlayerDeath");
}

} // namespace

int main() {
    testRegistrationAndSimpleTone();
    testBoundsAndHighVolumeBit();
    testCoalescingAndRateBudget();
    testStopBypassesBudget();
    testStopPreservesNewIntent();
    testMuteAndReenable();
    testOwnerLifetimeAndStalePointer();
    testTonesHighTerminatorAndRepeat();
    testTonesZeroTerminator();
    testDenseSequenceKeepsWallclock();
    testPlaytuneNotesWaitAndStop();
    testPlaytuneMarkRepeatTranspose();
    testPlaytuneInfiniteRepeatAndStop();
    testInvalidAndWorkCaps();
    testLongNoteRefresh();
    testRawPortPriorityOrdering();
    testZeroDurationAndPointerAmbiguity();
    testFacadeAndNoteConstants();
    testRealArduboy3dCorpusCues();
    printf("arduboy audio compat passed (%u captured events)\n", captured.count);
    return 0;
}
