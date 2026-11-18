// Host harness for the shared game-tone contract and the real UART->driver
// handoff. No HAL, FreeRTOS or PWM code is linked here.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <atomic>
#include <thread>

#include "FreeRTOS.h"
#include "game_tone.h"
#include "game_tone_service.h"

static int failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

using namespace CrabitTone;

static ParseResult parseText(const char* text, Tone* tone) {
    return CrabitTone::parse(text, strlen(text), tone);
}

static bool post(const char* text, uint32_t now, bool session = true, uint16_t id = 0) {
    return handleFrame(text, strlen(text), id, session, now);
}

static void testParser() {
    Tone tone;
    CHECK(parseText("100,10", &tone) == ParseResult::Cue);
    CHECK(tone.frequency_hz == 100 && tone.duration_ms == 10);
    CHECK(parseText("5000,250", &tone) == ParseResult::Cue);
    CHECK(tone.frequency_hz == 5000 && tone.duration_ms == 250);
    CHECK(parseText("440,30", &tone) == ParseResult::Cue);
    CHECK(tone.frequency_hz == 440 && tone.duration_ms == 30);
    CHECK(parseText("0100,010", &tone) == ParseResult::Cue); // still a decimal integer
    CHECK(parseText("0,0", &tone) == ParseResult::Stop);

    const char* invalid[] = {
        "", "0,10", "10,0", "99,10", "5001,250", "100,9", "100,251",
        "-100,10", "+100,10", "100,-10", "100,+10", "100,10x", "100,10,",
        "100,,10", ",10", "100", "100,", " 100,10", "100, 10", "100,10 ",
        "100.0,10", "1e3,10", "0x64,10", "65536,10", "999999999999,10",
        "100,65536", "100,999999999999", "100,10\n", "100\t,10", "1,2,3",
    };
    for (const char* text : invalid) {
        Tone rejected{};
        const ParseResult result = parseText(text, &rejected);
        if (result != ParseResult::Invalid) {
            fprintf(stderr, "FAIL parser accepted \"%s\"\n", text);
            ++failures;
        }
    }

    // Length is authoritative; an embedded NUL is not a terminator on the wire.
    const char embedded[] = {'1', '0', '0', ',', '1', '0', '\0', '1'};
    Tone rejected{};
    CHECK(CrabitTone::parse(embedded, sizeof(embedded), &rejected) == ParseResult::Invalid);
    CHECK(CrabitTone::parse(nullptr, 4, &rejected) == ParseResult::Invalid);
    CHECK(CrabitTone::parse("440,30", 6, nullptr) == ParseResult::Invalid);
}

static void testGateAndLatestValue() {
    reset();
    CHECK(!post("440,30", 1000, false));
    CHECK(take(1000) == kEmptyWord);
    CHECK(!post("440,30", 1000, true, 7)); // request_id must be 0
    CHECK(take(1000) == kEmptyWord);
    CHECK(!post("0,0", 1000, true, 7));
    CHECK(!post("bogus", 1000));
    CHECK(take(1000) == kEmptyWord);

    CHECK(post("440,30", 1000));
    CHECK(post("880,20", 1001)); // latest wins without a queue
    const uint32_t word = take(1002);
    const Tone tone = decode(word);
    CHECK(tone.frequency_hz == 880 && tone.duration_ms == 20);
    CHECK(take(1002) == kEmptyWord); // consumed once

    CHECK(post("0,0", 1003));
    CHECK(take(1003) == kStopWord);
    CHECK(take(1003) == kEmptyWord);
}

static void testLeaseAndWrap() {
    reset();
    CHECK(post("440,10", 5000));
    CHECK(take(5000 + kLeaseMs) != kEmptyWord); // accepted at the maximum age
    // Republish before testing expiry: the first take above consumed the cue, so
    // a second take would be empty for the wrong reason.
    reset();
    CHECK(post("440,10", 5000));
    CHECK(take(5000 + kLeaseMs + 1u) == kEmptyWord); // stale rejected directly

    // Age arithmetic wraps with HAL_GetTick, not with wall-clock milliseconds.
    reset();
    const uint32_t near_wrap = 0xFFFFFFFFu - 10u;
    CHECK(post("440,10", near_wrap));
    CHECK(take(near_wrap + kLeaseMs) != kEmptyWord);
    reset();
    CHECK(post("440,10", near_wrap));
    CHECK(take(near_wrap + kLeaseMs + 1u) == kEmptyWord);

    // discardPending covers mute/session loss/user priority in the driver.
    reset();
    CHECK(post("440,10", 7000));
    discardPending();
    CHECK(take(7000) == kEmptyWord);

    // A stop is a command, not an aged cue; age must never silently eat it.
    reset();
    CHECK(post("0,0", 8000));
    CHECK(take(8000 + kLeaseMs + 1) == kStopWord);
}

static void testInterleavedPublishIsAtomic() {
    reset();
    CHECK(post("440,30", 1000)); // old cue with its own old timestamp

    GameToneHostGate::arm();
    bool writer_ok = false;
    std::thread writer([&]() { writer_ok = post("880,20", 2000); });
    CHECK(GameToneHostGate::waitPaused()); // writer is inside the publication

    std::atomic<bool> consumer_done{false};
    uint32_t result = kEmptyWord;
    std::thread consumer([&]() {
        result = take(2000);
        consumer_done.store(true);
    });
    // Writer has entered once; this second entry is the blocked consumer.
    CHECK(GameToneHostGate::waitAttempts(2));
    CHECK(!consumer_done.load()); // cannot observe the half-finished publish

    GameToneHostGate::release();
    writer.join();
    consumer.join();

    CHECK(writer_ok);
    CHECK(consumer_done.load());
    const Tone tone = decode(result);
    // The consumer must see the new word with the new timestamp, never the old
    // 440/30 cue renewed by the new tick.
    CHECK(tone.frequency_hz == 880 && tone.duration_ms == 20);
}

static void testConcurrentMailbox() {
    reset();
    std::atomic<uint32_t> now{50000};
    std::atomic<bool> stop{false};
    std::atomic<int> posted{0};

    std::thread writer([&]() {
        uint32_t local = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            const uint32_t value = (local++ & 1u) ? 1200u : 3400u;
            char text[24];
            snprintf(text, sizeof(text), "%u,%u", value, 10u + (local % 200u));
            handleFrame(text, strlen(text), 0, true,
                        now.load(std::memory_order_relaxed));
            posted.fetch_add(1, std::memory_order_relaxed);
            if ((local % 97u) == 0)
                handleFrame("0,0", 3, 0, true, now.load(std::memory_order_relaxed));
        }
    });

    int consumed = 0;
    for (int step = 0; step < 20000; ++step) {
        now.fetch_add(50, std::memory_order_relaxed);
        const uint32_t word = take(now.load(std::memory_order_relaxed));
        if (word == kEmptyWord || word == kStopWord) continue;
        ++consumed;
        // A torn mailbox would surface a field the writer never sent.
        const Tone tone = decode(word);
        CHECK(tone.frequency_hz == 1200 || tone.frequency_hz == 3400);
        CHECK(tone.duration_ms >= 10 && tone.duration_ms <= 209);
    }
    stop.store(true, std::memory_order_relaxed);
    writer.join();
    CHECK(posted.load() > 1000);
    CHECK(consumed > 100);
}

int main() {
    testParser();
    testGateAndLatestValue();
    testLeaseAndWrap();
    testInterleavedPublishIsAtomic();
    testConcurrentMailbox();
    if (failures) {
        fprintf(stderr, "%d game-tone host checks failed\n", failures);
        return 1;
    }
    puts("STM32 game-tone host checks passed");
    return 0;
}
