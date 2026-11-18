// Host-only evidence for the on-demand port resource contract: allocation
// failure rollback, duplicate-launch refusal, retained save isolation, the
// zero-save access guard and repeated switching. Real port code and the real
// ArduboyPortRuntime are compiled; only the device stubs are fake.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define memcpy_P memcpy
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

// Registry order keeps the same macro neighbourhood as the shipped sketch.
#include "app/apps/games/ardubullets/app.h"
#include "app/apps/games/chiemagari/app.h"
#include "app/apps/games/ardugo/app.h"
#include "app/apps/games/galaxion/app.h"
#include "app/apps/games/asteroiddodge/app.h"
#include "app/apps/games/memoryrhythm/app.h"
#include "app/apps/games/pong/app.h"
#include "app/apps/games/longcat/app.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}

// ---------------------------------------------------------------------------
// Counting / failing allocator injected through the documented host hook.
// ---------------------------------------------------------------------------
static int alloc_fail_after = -1; // -1: never fail; 0: fail the next call
static long test_allocs = 0;
static long test_frees = 0;

static void *testAllocate(size_t bytes) {
    if (alloc_fail_after == 0) return nullptr;
    if (alloc_fail_after > 0) --alloc_fail_after;
    void *pointer = malloc(bytes);
    if (pointer) ++test_allocs;
    return pointer;
}

static void testRelease(void *pointer) {
    if (!pointer) return;
    ++test_frees;
    free(pointer);
}

static void resetAccounting() {
    // Release first: retained slots were allocated with the injected pair, so
    // dropping them must not be counted against the fresh baseline.
    CrabitArduboy::setAllocator(testAllocate, testRelease);
    CrabitArduboy::releaseRetainedSaves();
    test_allocs = test_frees = 0;
    alloc_fail_after = -1;
}

static uint32_t failuresNow() {
    return CrabitArduboy::allocationStats().failures;
}

static void expectBalanced() {
    const CrabitArduboy::detail::Stats &stats = CrabitArduboy::allocationStats();
    assert(test_allocs == test_frees);
    assert(stats.allocations == stats.releases);
    assert(stats.live == 0);
    assert(stats.live_bytes == 0);
    assert(stats.failures >= 0);
}

static void runBriefly(Application &app, Canvas &canvas, InputDriver &input, unsigned frames) {
    app.onMount(&canvas, &input);
    for (unsigned i = 0; i < frames; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
    }
    app.onUnmount();
}

static void duplicateLaunchIsRefused() {
    resetAccounting();
    Canvas canvas;
    InputDriver input;

    PongApp *first = CrabitArduboy::tryCreate<PongApp>();
    assert(first);
    runBriefly(*first, canvas, input, 10);

    // The port id is already claimed; the second creation must fail before
    // AppManager::launch and must not disturb the live Application.
    assert(CrabitArduboy::tryCreate<PongApp>() == nullptr);
    const CrabitArduboy::detail::Stats &stats = CrabitArduboy::allocationStats();
    assert(stats.live == 1);
    runBriefly(*first, canvas, input, 10);

    delete first;
    expectBalanced();
    puts("resource: duplicate launch refused");
}

static void allocationFailuresRollBack() {
    // State allocation failure.
    resetAccounting();
    const uint32_t stateFailures = failuresNow();
    alloc_fail_after = 0;
    assert(CrabitArduboy::tryCreate<PongApp>() == nullptr);
    assert(failuresNow() == stateFailures + 1);
    assert(CrabitArduboy::allocationStats().live == 0);
    alloc_fail_after = -1;
    // The admission bit was rolled back with the failed allocation.
    PongApp *ok = CrabitArduboy::tryCreate<PongApp>();
    assert(ok);
    delete ok;
    expectBalanced();

    // Save-slot allocation failure: the State is allocated, then the 1 KiB
    // session slot fails. The state must be destroyed and the id released.
    resetAccounting();
    const uint32_t saveFailures = failuresNow();
    alloc_fail_after = 1;
    assert(CrabitArduboy::tryCreate<AsteroidDodgeApp>() == nullptr);
    assert(failuresNow() == saveFailures + 1);
    assert(CrabitArduboy::allocationStats().live == 0);
    alloc_fail_after = -1;
    AsteroidDodgeApp *retry = CrabitArduboy::tryCreate<AsteroidDodgeApp>();
    assert(retry);
    delete retry;

    // A failure while another port is live leaves that port usable.
    resetAccounting();
    GalaxionApp *current = CrabitArduboy::tryCreate<GalaxionApp>();
    assert(current);
    const uint32_t liveFailureBase = failuresNow();
    alloc_fail_after = 0;
    assert(CrabitArduboy::tryCreate<LongCatApp>() == nullptr);
    assert(failuresNow() == liveFailureBase + 1);
    alloc_fail_after = -1;
    Canvas canvas;
    InputDriver input;
    runBriefly(*current, canvas, input, 10);
    delete current;
    expectBalanced();
    puts("resource: allocation failures rolled back");
}

static void savesPersistAndStayIsolated() {
    resetAccounting();
    Canvas canvas;
    InputDriver input;

    ArdubulletsApp *first = CrabitArduboy::tryCreate<ArdubulletsApp>();
    assert(first);
    Arduboy2Compat::RamEeprom &fresh = ArdubulletsSandbox::engine().eeprom;
    for (int address = 0; address < 1024; ++address) assert(fresh.read(address) == 0xFF);
    // The port's own writer, not raw byte pokes: its 10-byte AVR image sits at
    // 1,012..1,021 and the checksum word is the last two bytes of the window.
    first->onMount(&canvas, &input);
    for (int i = 0; i < 150; ++i) { fakeMillis += 17; first->onUpdate(fakeMillis); }
    assert(ArdubulletsSandbox::portState().mode.mode == ArdubulletsSandbox::MODE_TITLE);
    ArdubulletsSandbox::State &state = ArdubulletsSandbox::portState();
    state.common.record.gameSeed = 0x00112233;
    state.common.record.rank = 7;
    state.common.record.isCodeManual = false;
    state.common.record.isBlinkLED = false;
    state.common.record.isDrawEdge = false;
    state.common.record.isCleared = true;
    state.common.record.playFrames = 0x00000099;
    state.common.record.playCount = 0x0042;
    state.common.isRecordDirty = true;
    ArdubulletsSandbox::writeRecord();
    const uint8_t expected[16] = {
        0x4F, 0x42, 0x4E, 0x13, // signature 0x134E424F
        0x33, 0x22, 0x11, 0x87, // seed:24 | rank:4 | flags
        0x99, 0x00, 0x00, 0x00, // playFrames
        0x42, 0x00,             // playCount
        0x6A, 0xE1,             // checksum 0xE16A
    };
    for (int i = 0; i < 16; ++i) assert(fresh.bytes[1008 + i] == expected[i]);
    first->onUnmount();
    delete first; // commitSaveSlot runs while the engine is still alive

    ArdubulletsApp *again = CrabitArduboy::tryCreate<ArdubulletsApp>();
    assert(again);
    again->onMount(&canvas, &input);
    for (int i = 0; i < 150; ++i) { fakeMillis += 17; again->onUpdate(fakeMillis); }
    ArdubulletsSandbox::State &restored = ArdubulletsSandbox::portState();
    assert(restored.common.recordState == ArdubulletsSandbox::RECORD_STORED);
    assert(!restored.common.isRecordDirty);
    assert(restored.common.record.gameSeed == 0x00112233);
    assert(restored.common.record.rank == 7);
    assert(restored.common.record.isCleared && !restored.common.record.isCodeManual);
    assert(restored.common.record.playFrames == 0x00000099);
    assert(restored.common.record.playCount == 0x0042);
    again->onUnmount();
    delete again;

    // A different 1 KiB port must start from erased bytes, not share the slot.
    ChiemagariApp *other = CrabitArduboy::tryCreate<ChiemagariApp>();
    assert(other);
    Arduboy2Compat::RamEeprom &isolated = ChiemagariSandbox::engine().eeprom;
    assert(isolated.read(17) == 0xFF);
    assert(isolated.read(1008) == 0xFF);
    assert(isolated.read(1022) == 0xFF);
    delete other;

    // Reopening the first port still sees its own record.
    ArdubulletsApp *third = CrabitArduboy::tryCreate<ArdubulletsApp>();
    assert(third);
    Arduboy2Compat::RamEeprom &kept = ArdubulletsSandbox::engine().eeprom;
    for (int i = 0; i < 16; ++i) assert(kept.bytes[1008 + i] == expected[i]);
    delete third;

    // Only the two real save slots (ardubullets, chiemagari) were allocated.
    CrabitArduboy::releaseRetainedSaves();
    expectBalanced();
    puts("resource: saves persist and stay isolated");
}

static void zeroSaveGuardAndOobSemantics() {
    resetAccounting();
    PongApp *pong = CrabitArduboy::tryCreate<PongApp>();
    assert(pong);
    PongSandbox::State &state = PongSandbox::portState();
    state.unexpectedEepromAccess = 0;
    state.arduboy.eeprom.update(5, 0x11);
    assert(state.unexpectedEepromAccess == 1);
    assert(state.arduboy.eeprom.read(5) == 0x11);
    assert(state.unexpectedEepromAccess == 2);
    // Bounded logical space kept: OOB reads are 0xFF, OOB writes discard.
    assert(state.arduboy.eeprom.read(1024) == 0xFF);
    assert(state.arduboy.eeprom.read(-1) == 0xFF);
    state.arduboy.eeprom.update(1024, 0x22);
    state.arduboy.eeprom.update(-1, 0x22);
    assert(state.arduboy.eeprom.read(1024) == 0xFF);
    assert(state.arduboy.eeprom.read(-1) == 0xFF);
    assert(state.unexpectedEepromAccess >= 6);
    delete pong;
    // No slot was allocated for a zero-save port and nothing was committed.
    expectBalanced();

    PongApp *reopened = CrabitArduboy::tryCreate<PongApp>();
    assert(reopened);
    assert(PongSandbox::portState().arduboy.eeprom.read(5) == 0xFF);
    delete reopened;
    puts("resource: zero-save guard, OOB semantics, no hidden slot");
}

static void repeatedSwitchingStaysStable() {
    resetAccounting();
    Canvas canvas;
    InputDriver input;
    bool asteroidSaveSlot = false;
    bool longCatSaveSlot = false;
    for (int round = 0; round < 12; ++round) {
        // The two 1 KiB ports retain their session slot after the first visit;
        // their state allocation must still balance every round.
        long marker = test_allocs - test_frees;
        AsteroidDodgeApp *save = CrabitArduboy::tryCreate<AsteroidDodgeApp>();
        assert(save);
        runBriefly(*save, canvas, input, 4);
        delete save;
        assert(test_allocs - test_frees == marker + (asteroidSaveSlot ? 0 : 1));
        asteroidSaveSlot = true;

        marker = test_allocs - test_frees;
        GalaxionApp *zero = CrabitArduboy::tryCreate<GalaxionApp>();
        assert(zero);
        runBriefly(*zero, canvas, input, 4);
        delete zero;
        assert(test_allocs - test_frees == marker);

        marker = test_allocs - test_frees;
        LongCatApp *embedded = CrabitArduboy::tryCreate<LongCatApp>();
        assert(embedded);
        runBriefly(*embedded, canvas, input, 4);
        delete embedded;
        assert(test_allocs - test_frees == marker + (longCatSaveSlot ? 0 : 1));
        longCatSaveSlot = true;

        marker = test_allocs - test_frees;
        MemoryRhythmApp *sprites = CrabitArduboy::tryCreate<MemoryRhythmApp>();
        assert(sprites);
        runBriefly(*sprites, canvas, input, 4);
        delete sprites;
        assert(test_allocs - test_frees == marker);
    }
    // Retained save slots are the only survivors of the whole switching loop.
    assert(CrabitArduboy::allocationStats().live == 2);
    CrabitArduboy::releaseRetainedSaves();
    expectBalanced();
    puts("resource: repeated switching stays stable");
}

int main() {
    duplicateLaunchIsRefused();
    allocationFailuresRollBack();
    savesPersistAndStayIsolated();
    zeroSaveGuardAndOobSemantics();
    repeatedSwitchingStaysStable();
    puts("arduboy resource contract passed");
    return 0;
}
