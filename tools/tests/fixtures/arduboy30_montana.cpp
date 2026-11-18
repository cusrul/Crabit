// Actual Montana source + actual compat adapter; only hardware/NanoUI mocked.
// Reproduces the launcher's inherited include order and macro pollution.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define STATE_TITLE 0
#define STATE_GAME_OVER 3
#define FPS 60
#define PROGMEM
#define pgm_read_byte(x) (*((uint8_t *)(x)))
#define pgm_read_word(x) (*((uint16_t *)(x)))
// The bad historical 16-bit pointer macro must not be used for any pointer.
#define pgm_read_ptr(x) (*((uint16_t *)(x)))
#ifdef bitToggle
#error "Arduino has no bitToggle macro"
#endif

#include "app/apps/games/montana/app.h"

static_assert(STATE_TITLE == 0 && STATE_GAME_OVER == 3 && FPS == 60,
              "legacy launcher macros must survive");
#ifndef pgm_read_ptr
#error "fixture must provide the launcher's pgm_read_ptr macro"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace S = MontanaSandbox;
static float g_ax = 0, g_ay = 0;
static void level() { g_ax = 0; g_ay = 0; }
static void frames(Application& app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax; AmebaStore::imu_ay = g_ay; AmebaStore::imu_az = 1.0f;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
        assert(!app.exitRequested());
    }
}
static void tap(Application& app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 6);
}
// Hold a tilt direction across a 30 Hz render tick, then return to level so
// the next step produces a fresh edge.
static void tiltStep(Application& app, int dx, int dy) {
    g_ax = dx * 0.5f; g_ay = dy * 0.5f;
    frames(app, 3);
    level();
    frames(app, 5);
}
static void cursorTo(Application& app, int tx, int ty) {
    for (int guard = 0; guard < 80 && (S::Cx != tx || S::Cy != ty); ++guard) {
        if (S::Cx < tx) tiltStep(app, 1, 0);
        else if (S::Cx > tx) tiltStep(app, -1, 0);
        else if (S::Cy < ty) tiltStep(app, 0, 1);
        else if (S::Cy > ty) tiltStep(app, 0, -1);
    }
    assert(S::Cx == tx && S::Cy == ty);
}
static void waitForGame(Application& app, int target, unsigned cap = 900) {
    for (unsigned i = 0; i < cap && S::game != target; ++i) frames(app, 1);
    assert(S::game == target);
}
static void exitHold(Application& app) {
    AmebaStore::raw_btn_mask = 1U << 6;
    fakeMillis += 17;
    app.onUpdate(fakeMillis);
}
static uint32_t digest(const uint8_t *bytes) {
    uint32_t h = 2166136261U;
    for (int i = 0; i < 1024; ++i) h = (h ^ bytes[i]) * 16777619U;
    return h;
}
static unsigned setBits(const uint8_t *bytes) {
    unsigned total = 0;
    for (int i = 0; i < 1024; ++i) {
        for (int b = 0; b < 8; ++b) total += (bytes[i] >> b) & 1;
    }
    return total;
}
static void image(const char *folder, const char *name, const Arduboy2Compat &engine) {
    uint8_t zeros[1024] = {};
    assert(digest(engine.sBuffer) != digest(zeros));
    if (!folder) return;
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.pbm", folder, name);
    FILE *file = fopen(path, "wb"); assert(file);
    fputs("P4\n128 64\n", file);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; x += 8) {
        uint8_t bits = 0;
        for (int b = 0; b < 8; ++b)
            if (!(engine.sBuffer[y / 8 * 128 + x + b] & (1U << (y & 7)))) bits |= 1U << (7 - b);
        fputc(bits, file);
    }
    assert(!fclose(file));
}
static bool isTwo(int v) { return v == 1 || v == 14 || v == 27 || v == 40; }
static int gapCount() {
    int n = 0;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 13; ++x)
        if (S::spel[x][y] == 98) ++n;
    return n;
}
static int cardCount() {
    int n = 0;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 13; ++x)
        if (S::spel[x][y] < 98) ++n;
    return n;
}
// Same target search the original verplaats() performs.
static bool canMoveValue(int v, int *tx, int *ty) {
    if (isTwo(v)) {
        for (int y = 0; y < 4; ++y)
            if (S::spel[0][y] == 98) { *tx = 0; *ty = y; return true; }
    }
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 12; ++x) {
            if (S::spel[x][y] == v - 1 && S::spel[x + 1][y] == 98) {
                *tx = x + 1; *ty = y; return true;
            }
        }
    }
    return false;
}
static bool findCard(bool legal, int *mx, int *my, int *tx, int *ty) {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 13; ++x) {
            const int v = S::spel[x][y];
            if (v >= 98) continue;
            int gx = 0, gy = 0;
            const bool movable = canMoveValue(v, &gx, &gy);
            if (movable == legal) { *mx = x; *my = y; *tx = gx; *ty = gy; return true; }
        }
    }
    return false;
}
static void seedWin() {
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 12; ++x) S::spel[x][y] = y * 13 + x + 1;
        S::spel[12][y] = 98;
    }
    S::Cx = 0; S::Cy = 0;
}

int main(int argc, char **argv) {
    const char *folder = argc > 1 ? argv[1] : nullptr;
    Point p = {1, 2}; Rect r = {0, 0, 8, 8};
    assert(p.x == 1 && r.width() == 8);
    Canvas canvas; InputDriver input;
    MontanaApp app;
    app.onMount(&canvas, &input);

    // The retained compressed splash must decode to the original 128x64 art.
    frames(app, 3);
    assert(S::game == 0 && S::introPhase == 0);
    assert(setBits(S::arduboy.sBuffer) == 4357);
    image(folder, "montana-splash", S::arduboy);

    // Wait out the original two-second splash, then enter the options.
    for (int i = 0; i < 200 && S::introPhase == 0; ++i) frames(app, 1);
    assert(S::introPhase == 1);
    image(folder, "montana-intro", S::arduboy);
    tap(app, 1U << 4);
    assert(S::game == 7);
    for (int i = 0; i < 20 && S::optionPhase == 0; ++i) frames(app, 1);
    assert(S::optionPhase == 1);
    image(folder, "montana-options", S::arduboy);

    // Hint = yes (A), shuffle limit = yes (A): deterministically exercises
    // both limited-play paths without inventing any option.
    tap(app, 1U << 4);
    assert(S::hint == 1);
    for (int i = 0; i < 20 && S::optionPhase < 3; ++i) frames(app, 1);
    assert(S::optionPhase == 3);
    tap(app, 1U << 4);
    assert(S::eind == 1);
    waitForGame(app, 3);
    assert(gapCount() == 4);
    assert(cardCount() == 48);
    image(folder, "montana-deal", S::arduboy);

    // Real card selection and legal move through the shared tilt d-pad.
    int mx = 0, my = 0, tx = 0, ty = 0;
    assert(findCard(true, &mx, &my, &tx, &ty));
    const int movedValue = S::spel[mx][my];
    cursorTo(app, mx, my);
    tap(app, 1U << 4);
    frames(app, 4);
    assert(S::game == 3);
    assert(S::tip == 0);
    assert(S::spel[tx][ty] == movedValue);
    assert(S::spel[mx][my] == 98);
    assert(gapCount() == 4);
    image(folder, "montana-move", S::arduboy);

    // An immovable card is refused by the original rule and opens the hint
    // screen only because the player enabled Hint.
    int ix = 0, iy = 0, unusedX = 0, unusedY = 0;
    assert(findCard(false, &ix, &iy, &unusedX, &unusedY));
    cursorTo(app, ix, iy);
    tap(app, 1U << 4);
    frames(app, 4);
    assert(S::game == 10);
    assert(S::tip == 1);
    const bool blinkBefore = S::hintKnipper;
    frames(app, 40);
    assert(S::hintKnipper != blinkBefore);
    for (int i = 0; i < 80 && S::hintVerder != 1; ++i) frames(app, 1);
    assert(S::hintVerder == 1);
    image(folder, "montana-hint", S::arduboy);
    tap(app, 1U << 4);
    assert(S::game == 3);

    // One real limited reshuffle.
    const int dealBefore = S::delen;
    tap(app, 1U << 5);
    for (int i = 0; i < 40 && S::game == 3; ++i) frames(app, 1);
    waitForGame(app, 3);
    assert(S::delen == dealBefore - 1);
    assert(gapCount() == 4);
    assert(cardCount() == 48);

    // Seeded legal terminal position, not a full input-driven game: every row
    // is 2..K in order and the fourth ace is the remaining gap.
    seedWin();
    frames(app, 4);
    assert(S::game == 9);
    for (int i = 0; i < 120 && S::winPhase == 0; ++i) frames(app, 1);
    assert(S::winPhase == 1);
    image(folder, "montana-win", S::arduboy);
    tap(app, 1U << 4);
    waitForGame(app, 3);
    assert(S::delen == 3);

    // Three limited reshuffles exhaust the original counter and reach its
    // own lost screen; A then starts the original new game.
    for (int round = 1; round <= 3; ++round) {
        tap(app, 1U << 5);
        if (round < 3) {
            for (int i = 0; i < 40 && S::game == 3; ++i) frames(app, 1);
            waitForGame(app, 3);
            assert(S::delen == 3 - round);
        } else {
            waitForGame(app, 8);
            assert(S::delen == 0);
        }
    }
    for (int i = 0; i < 120 && S::losePhase == 0; ++i) frames(app, 1);
    assert(S::losePhase == 1);
    image(folder, "montana-lost", S::arduboy);
    tap(app, 1U << 4);
    waitForGame(app, 3);
    assert(S::delen == 3 && S::eind == 1 && S::hint == 1);

    // Shared exit gesture and full transient reset on reentry.
    exitHold(app);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 100);
    bool cleared = true;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x)
        cleared &= canvas.pixels[y][x] == 0;
    assert(cleared);
    for (int i = 0; i < 1024; ++i) assert(S::arduboy.eeprom.bytes[i] == 0xFF);

    MontanaApp reopened;
    reopened.onMount(&canvas, &input);
    frames(reopened, 10);
    assert(S::game == 0 && S::introPhase == 0 && !S::introCredit);
    assert(S::optionPhase == 0 && S::losePhase == 0 && S::winPhase == 0);
    assert(S::hint == 2 && S::eind == 2 && S::delen == 3);
    assert(S::Cx == 0 && S::Cy == 0 && S::tip == 0);
    assert(S::hintVerder == 0 && !S::hintKnipper && S::hintWacht == 0);
    assert(gapCount() == 0 && cardCount() == 0);
    assert(setBits(S::arduboy.sBuffer) > 0);
    image(folder, "montana-reentry", S::arduboy);
    exitHold(reopened);
    assert(reopened.exitRequested());
    reopened.onUnmount();

    puts("montana: splash/intro/options/deal/move/hint/reshuffle/win/lose/exit/reentry passed");
    return 0;
}
