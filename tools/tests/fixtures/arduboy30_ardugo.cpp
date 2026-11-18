// Actual ArduGo sources + actual compat adapter; only hardware/NanoUI mocked.
// Reproduces the launcher's inherited include order and macro pollution.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Real NanoUI geometry first, as the product launcher does. If compat leaks a
// global Point/Rect this compile fails instead of being hidden by test fakes.
#include "NanoUI/compatibility/geometry.h"

// Macros left behind by earlier games in launcher.h (Castle Boy, Arduboy 3D).
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define STATE_TITLE 0
#define STATE_STAGE_INTRO 1
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_GAME_FINISHED 4
#define FPS 60
#define PROGMEM
#define pgm_read_byte(x) (*((uint8_t *)(x)))
#define pgm_read_word(x) (*((uint16_t *)(x)))
// The bad historical 16-bit pointer macro: ports must not rely on reading a
// pointer through it (the real launcher also leaves arduboy3d's macro behind).
#define pgm_read_ptr(x) (*((uint16_t *)(x)))
#ifdef bitToggle
#error "Arduino has no bitToggle macro; compat must expose a function, not a macro"
#endif

#include "app/apps/games/ardugo/app.h"

static_assert(STATE_TITLE == 0 && STATE_STAGE_INTRO == 1 && STATE_PLAY == 2 &&
              STATE_GAME_OVER == 3 && STATE_GAME_FINISHED == 4 && FPS == 60,
              "legacy launcher macros must survive the port");
#ifndef pgm_read_ptr
#error "fixture must provide the launcher's pgm_read_ptr macro"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace S = ArduGoSandbox;
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
    frames(app, 5);
}
static void tiltTap(Application& app, float ax, float ay) {
    g_ax = ax; g_ay = ay;
    frames(app, 1);
    level();
    frames(app, 5);
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
static uint32_t blankDigest() {
    uint8_t zeros[1024] = {};
    return digest(zeros);
}
static void image(const char *folder, const char *name, const Arduboy2Compat &engine) {
    assert(digest(engine.sBuffer) != blankDigest());
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
static int stoneAt(uint8_t row, uint8_t col) { return S::portState().common.board.pointAt(row, col) & 0x03; }
static int countStones(uint8_t color) {
    int n = 0;
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c) n += (stoneAt(r, c) == color);
    return n;
}

// Catalogue Apps own an on-demand port State; only CrabitArduboy::tryCreate
// prepares the engine/framebuffer, the retained save slot and the single-live
// claim. A raw stack object would leave every sandbox pointer null.
template <class App>
static App &requireApp() {
    App *app = CrabitArduboy::tryCreate<App>();
    assert(app && "tryCreate must prepare the port resources");
    return *app;
}

int main(int argc, char **argv) {
    const char *folder = argc > 1 ? argv[1] : nullptr;
    // Real geometry compatibility check: these types must remain the NanoUI
    // ones (left/top/right/bottom) after the compat header is included.
    Point point = {-1, 3};
    Rect rect = {0, 0, 4, 4};
    assert(point.x == -1 && rect.width() == 4 && !rect.intersects({10, 10, 12, 12}));

    Canvas canvas; InputDriver input;
    ArduGoApp &app = requireApp<ArduGoApp>();
    app.onMount(&canvas, &input);
    frames(app, 10);
    assert(S::cur_sidebar == S::TITLE_SIDEBAR);
    assert(S::black_live == 0 && S::white_live == 0 && S::black_capt == 0 && S::white_capt == 0);
    assert(S::portState().common.board.cursorRow() == 4 && S::portState().common.board.cursorCol() == 4);
    image(folder, "ardugo-title", S::portState().common.arduboy);

    // Real tilt navigation: UP increments the logical row, LEFT decrements col.
    const int row0 = S::portState().common.board.cursorRow(), col0 = S::portState().common.board.cursorCol();
    tiltTap(app, 0, -0.5f);
    assert(S::portState().common.board.cursorRow() == row0 + 1);
    tiltTap(app, -0.5f, 0);
    assert(S::portState().common.board.cursorCol() == col0 - 1);

    // First real A placement: one black stone, one random legal white reply.
    S::portState().common.board.setCursor(4, 4);
    srand(1);
    tap(app, 1U << 4);
    assert(S::black_live == 1 && S::white_live == 1);
    assert(stoneAt(4, 4) == S::portState().common.board.BLACK_STONE);
    assert(countStones(S::portState().common.board.BLACK_STONE) == 1);
    assert(countStones(S::portState().common.board.WHITE_STONE) == 1);
    assert(S::cur_sidebar == S::STATS_SIDEBAR);
    const int aiRow = S::portState().common.board.cursorRow(), aiCol = S::portState().common.board.cursorCol();
    assert(stoneAt(aiRow, aiCol) == S::portState().common.board.WHITE_STONE);
    assert(!(aiRow == 4 && aiCol == 4));
    image(folder, "ardugo-play", S::portState().common.arduboy);

    // A on the occupied AI stone is an illegal move: no stones or counters move.
    tap(app, 1U << 4);
    assert(S::black_live == 1 && S::white_live == 1);
    assert(S::black_capt == 0 && S::white_capt == 0);
    assert(stoneAt(aiRow, aiCol) == S::portState().common.board.WHITE_STONE);

    // B restart uses the original clear path.
    tap(app, 1U << 5);
    assert(S::black_live == 0 && S::white_live == 0 && S::black_capt == 0 && S::white_capt == 0);
    assert(countStones(1) == 0 && countStones(2) == 0);
    assert(S::portState().common.board.cursorRow() == 4 && S::portState().common.board.cursorCol() == 4);
    assert(S::cur_sidebar == S::TITLE_SIDEBAR);

    // Real core move/liberty/capture logic: surround a white stone in the corner.
    S::portState().common.board.playMove(0, 0, S::portState().common.board.WHITE_STONE);
    assert(S::white_live == 1 && stoneAt(0, 0) == S::portState().common.board.WHITE_STONE);
    S::portState().common.board.playMove(0, 1, S::portState().common.board.BLACK_STONE);
    assert(S::white_capt == 0);
    S::portState().common.board.playMove(1, 0, S::portState().common.board.BLACK_STONE);
    assert(S::white_capt == 1); // black prisoner count
    assert(S::white_live == 0);
    assert(stoneAt(0, 0) == 0);
    assert(S::black_live == 2);
    image(folder, "ardugo-capture", S::portState().common.arduboy);
    assert(countStones(1) == 0);

    // Characterize the upstream "simple rules" gap: a black stone placed with
    // no liberties is not rejected; the shared capture pass removes it and
    // counts it as Black's loss. This is original behavior, not a port fix.
    tap(app, 1U << 5);
    S::portState().common.board.playMove(4, 3, S::portState().common.board.WHITE_STONE);
    S::portState().common.board.playMove(4, 5, S::portState().common.board.WHITE_STONE);
    S::portState().common.board.playMove(3, 4, S::portState().common.board.WHITE_STONE);
    S::portState().common.board.playMove(5, 4, S::portState().common.board.WHITE_STONE);
    assert(S::white_live == 4);
    S::portState().common.board.playMove(4, 4, S::portState().common.board.BLACK_STONE);
    assert(stoneAt(4, 4) == 0);
    assert(S::black_live == 0);
    assert(S::black_capt == 1);
    assert(S::white_live == 4);

    // Restart, then a fresh deterministic AI reply is on an empty point instead
    // of overlapping the black stone.
    tap(app, 1U << 5);
    srand(7);
    S::portState().common.board.setCursor(2, 2);
    tap(app, 1U << 4);
    assert(S::black_live == 1 && S::white_live == 1);
    assert(stoneAt(2, 2) == S::portState().common.board.BLACK_STONE);
    int whiteCells = countStones(S::portState().common.board.WHITE_STONE);
    assert(whiteCells == 1);
    for (int r = 0; r < 9; ++r)
        for (int c = 0; c < 9; ++c)
            if (stoneAt(r, c) == S::portState().common.board.WHITE_STONE) assert(!(r == 2 && c == 2));

    // Shared hold exit, canvas detach, then reentry starts a clean game.
    exitHold(app);
    assert(app.exitRequested());
    app.onUnmount();
    bool cleared = true;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) cleared &= canvas.pixels[y][x] == 0;
    assert(cleared);
    assert(canvas.displays > 20);

    delete &app;
    ArduGoApp &reopened = requireApp<ArduGoApp>();
    reopened.onMount(&canvas, &input);
    frames(reopened, 10);
    assert(S::cur_sidebar == S::TITLE_SIDEBAR);
    assert(S::black_live == 0 && S::white_live == 0 && S::black_capt == 0 && S::white_capt == 0);
    assert(countStones(1) == 0 && countStones(2) == 0);
    assert(S::portState().common.board.cursorRow() == 4 && S::portState().common.board.cursorCol() == 4);
    S::portState().common.board.setCursor(3, 3);
    srand(3);
    tap(reopened, 1U << 4);
    assert(S::black_live == 1 && S::white_live == 1);
    image(folder, "ardugo-reentry", S::portState().common.arduboy);
    exitHold(reopened);
    assert(reopened.exitRequested());
    reopened.onUnmount();

    delete &reopened;

    puts("ardugo: title/tilt/place/illegal/capture/score/AI/restart/exit/reentry passed");
    return 0;
}
