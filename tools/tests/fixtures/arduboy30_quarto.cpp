// Actual Quarto! sources + actual compat adapter; only hardware/NanoUI mocked.
// Reproduces the launcher's inherited include order and macro pollution.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

// Real NanoUI geometry first, as the product launcher does.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define STATE_TITLE 0
#define FPS 60
#define PROGMEM
#define pgm_read_byte(x) (*((uint8_t *)(x)))
#define pgm_read_word(x) (*((uint16_t *)(x)))
// The bad historical 16-bit pointer macro: the port must not read pointers
// through it (the real launcher also leaves arduboy3d's macro behind).
#define pgm_read_ptr(x) (*((uint16_t *)(x)))
#ifdef bitToggle
#error "Arduino has no bitToggle macro; compat must expose a function, not a macro"
#endif

#include "app/apps/games/quarto/app.h"

static_assert(STATE_TITLE == 0 && FPS == 60,
              "legacy launcher macros must survive the port");
#ifndef pgm_read_ptr
#error "fixture must provide the launcher's pgm_read_ptr macro"
#endif
#undef STATE_TITLE

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace S = QuartoSandbox;
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
static void image(const char *folder, const char *name, const Canvas &canvas,
                  const Arduboy2Compat &engine) {
    uint8_t zeros[1024] = {};
    assert(digest(engine.sBuffer) != digest(zeros));
    if (!folder) return;
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.pbm", folder, name);
    FILE *file = fopen(path, "wb"); assert(file);
    fputs("P4\n128 64\n", file);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; x += 8) {
        uint8_t bits = 0;
        for (int b = 0; b < 8; ++b)
            if (!canvas.pixels[y][x + b]) bits |= 1U << (7 - b);
        fputc(bits, file);
    }
    assert(!fclose(file));
}
static int stones() {
    int n = 0;
    for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x)
        if (S::portState().game.game.board[y][x] != 0xFF) ++n;
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
    Point p = {1, 2}; Rect r = {0, 0, 8, 8};
    assert(p.x == 1 && r.width() == 8);
    Canvas canvas; InputDriver input;
    QuartoApp &app = requireApp<QuartoApp>();
    app.onMount(&canvas, &input);
    frames(app, 10);
    frames(app, 120);
    assert(S::portState().mode.mode == S::MODE_TITLE);
    assert(S::Title::portState().state == S::Title::STATE_TITLE);
    assert(S::getMenuItemCount() == 6);
    assert(S::getMenuItemPos() == 0);
    image(folder, "quarto-title", canvas, S::portState().common.arduboy);

    // Title menu: YOU FIRST -> CPU FIRST -> 2 PLAYERS, then B starts.
    tiltTap(app, 0, 0.5f);
    tiltTap(app, 0, 0.5f);
    assert(S::getMenuItemPos() == 2);
    tap(app, 1U << 4);
    assert(S::portState().mode.mode == S::MODE_GAME);
    frames(app, 20);
    assert(S::portState().game.state == S::STATE_PLACING);
    assert(stones() == 0);
    assert(S::portState().common.record.playCount == 1);
    assert(S::portState().common.record.gameMode == S::GAME_MODE_2PLAYERS);
    assert(S::portState().common.record.playFrames > 0);
    image(folder, "quarto-start", canvas, S::portState().common.arduboy);

    // Real placement at (0,0), then the real 16-frame move animation.
    assert(S::portState().game.game.board[0][0] == 0xFF);
    const uint8_t firstPiece = S::portState().game.game.currentPiece;
    tap(app, 1U << 4);
    assert(S::portState().game.state == S::STATE_POST_PLACE);
    frames(app, 20);
    assert(S::portState().game.state == S::STATE_CHOOSING);
    assert(S::portState().game.game.board[0][0] == firstPiece);
    assert(stones() == 1);

    // Real piece choice: tilt moves the offer, B picks it for the opponent.
    const uint8_t oldPiece = S::portState().game.cursorPiece;
    tiltTap(app, 0.5f, 0);
    assert(S::portState().game.cursorPiece != oldPiece);
    tap(app, 1U << 4);
    assert(S::portState().game.state == S::STATE_PRE_PLACE);
    assert(S::portState().game.game.turn == 1);
    frames(app, 20);
    assert(S::portState().game.state == S::STATE_PLACING);
    image(folder, "quarto-play", canvas, S::portState().common.arduboy);

    // Occupied cell: the game refuses the placement and keeps playing.
    S::portState().game.cursorX = 0; S::portState().game.cursorY = 0;
    const uint8_t occupied = S::portState().game.game.board[0][0];
    tap(app, 1U << 4);
    assert(S::portState().game.state == S::STATE_PLACING);
    assert(S::portState().game.game.board[0][0] == occupied);
    assert(stones() == 1);

    // Seeded legal position: row 0 already has pieces 0, 2, 4 (all share
    // attribute bit 3 = 0); piece 6 at (3,0) completes the real winning line.
    memset(S::portState().game.game.board, 0xFF, sizeof(S::portState().game.game.board));
    S::portState().game.game.board[0][0] = 0;
    S::portState().game.game.board[0][1] = 2;
    S::portState().game.game.board[0][2] = 4;
    S::portState().game.game.currentPiece = 6;
    S::portState().game.game.restPieces = 0xFFFF & ~(1 << 6);
    S::portState().game.game.turn = 3;
    S::portState().game.cursorX = 3; S::portState().game.cursorY = 0;
    S::portState().common.record.gameMode = S::GAME_MODE_2PLAYERS;
    S::portState().common.record.settings &= ~0x1;
    S::portState().game.state = S::STATE_PLACING;
    tap(app, 1U << 4);
    assert(S::portState().game.state == S::STATE_POST_PLACE);
    frames(app, 20);
    assert(S::portState().game.state == S::STATE_OVER);
    assert(S::portState().game.game.board[0][3] == 6);
    assert(S::portState().game.resultLabel != nullptr);
    assert(strcmp(reinterpret_cast<const char *>(S::portState().game.resultLabel), "2P WINS") == 0);
    frames(app, 2);
    image(folder, "quarto-win", canvas, S::portState().common.arduboy);
    const uint16_t savedCount = S::portState().common.record.playCount;
    assert(savedCount == 1 && S::portState().common.record.playFrames > 0);

    // Over screen: A opens the real menu; BACK TO TITLE leaves the game.
    frames(app, 125);
    tap(app, 1U << 5);
    assert(S::portState().game.state == S::STATE_MENU);
    assert(S::getMenuItemCount() == 2);
    tiltTap(app, 0, 0.5f);
    assert(S::getMenuItemPos() == 1);
    tap(app, 1U << 4);
    assert(S::portState().game.state == S::STATE_LEAVE);
    frames(app, 2);
    assert(S::portState().mode.mode == S::MODE_TITLE);
    // Returning to the title re-reads the record that finalizeGame stored.
    const uint32_t savedFrames = S::portState().common.record.playFrames;
    assert(savedFrames > 0);

    // Persist a real setting through the original record writer; initTitle
    // re-reads it and applies the framebuffer inversion on reentry.
    S::portState().common.record.settings |= 0x4;
    S::portState().common.isRecordDirty = true;
    S::writeRecord();

    exitHold(app);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 50);

    delete &app;
    QuartoApp &reopened = requireApp<QuartoApp>();
    reopened.onMount(&canvas, &input);
    frames(reopened, 130);
    assert(S::portState().mode.mode == S::MODE_TITLE);
    assert(S::portState().common.record.playCount == savedCount);
    assert(S::portState().common.record.playFrames == savedFrames);
    assert((S::portState().common.record.settings & 0x4) != 0);
    assert(S::portState().common.isScreenInverted);
    // The displayed canvas is the inverted copy while sBuffer itself stays
    // restored, so incremental drawing continues correctly next frame.
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) {
        uint8_t on = (S::portState().common.arduboy.sBuffer[(y / 8) * 128 + x] >> (y & 7)) & 1;
        assert(canvas.pixels[y][x] == (on ? 0 : 1));
    }
    image(folder, "quarto-reentry", canvas, S::portState().common.arduboy);
    exitHold(reopened);
    assert(reopened.exitRequested());
    reopened.onUnmount();

    delete &reopened;

    puts("quarto: title/start/place/choose/illegal/win/record/exit/reentry passed");
    return 0;
}
