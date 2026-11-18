// Actual Chie Magari Ita sources + actual new adapters; only hardware/NanoUI
// are mocked. Deterministic input script, no image generation.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define bitRead(x,b) (((x) >> (b)) & 1UL)
#define bitSet(x,b) ((x) |= 1UL << (b))
#define bitClear(x,b) ((x) &= ~(1UL << (b)))
#define memcpy_P memcpy
// Launcher macros must survive the port's own include.
#define STATE_TITLE 0
#define FPS 60
#include "app/apps/games/chiemagari/app.h"
static_assert(STATE_TITLE == 0 && FPS == 60, "launcher macros must survive");
// The launcher keeps these as macros for later games; drop them here so the
// test can name this port's own menu states.
#undef STATE_TITLE

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace Sandbox = ChiemagariSandbox;
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
// The adapter stretches a 20 ms telemetry pulse to three frames, so leave a
// gap after each tap or the next one cannot produce a fresh edge.
static void tap(Application& app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 5);
}
static void exitHold(Application& app) {
    AmebaStore::raw_btn_mask = 1U << 6; // PAT hold >= 800 ms is owned by STM32
    fakeMillis += 17;
    app.onUpdate(fakeMillis);
}
// Directions come from tilt on this device; one fresh tilt frame is one step.
static void tiltTap(Application& app, float ax, float ay) {
    g_ax = ax; g_ay = ay;
    frames(app, 1);
    level();
    frames(app, 5);
}
static uint32_t digest(const uint8_t* bytes) {
    uint32_t h = 2166136261U;
    for (int i = 0; i < 1024; ++i) h = (h ^ bytes[i]) * 16777619U;
    return h;
}
static uint32_t blankDigest() {
    uint8_t zeros[1024] = {};
    return digest(zeros);
}
static void image(const char* folder, const char* name, const Arduboy2Compat& engine) {
    assert(digest(engine.sBuffer) != blankDigest());
    if (!folder) return;
    char path[1024]; snprintf(path, sizeof(path), "%s/%s.pbm", folder, name);
    FILE* file = fopen(path, "wb"); assert(file);
    fputs("P4\n128 64\n", file);
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; x += 8) {
        uint8_t bits = 0;
        for (int b = 0; b < 8; ++b)
            if (!(engine.sBuffer[y/8*128+x+b] & (1U << (y&7)))) bits |= 1U << (7-b);
        fputc(bits, file);
    }
    assert(!fclose(file));
}

// The solved packing below was found with the game's own piece/board rules
// (all body cells disjoint, all cells on the frame's free lattice) and is
// seeded into pieceAry; every transition, codec call and check stays real.
static const int solution[10][4] = {
    {0, 7, 8, 0}, {1, 4, 3, 1}, {7, 9, 3, 0}, {9, 10, 6, 7}, {4, 11, 7, 7},
    {2, 12, 2, 3}, {5, 9, 1, 5}, {6, 7, 3, 2}, {3, 7, 1, 6}, {8, 6, 6, 0},
};
static void seedSolvedPieces() {
    for (int i = 0; i < 10; ++i) {
        const int p = solution[i][0];
        Sandbox::portState().common.pieceAry[p].x = solution[i][1];
        Sandbox::portState().common.pieceAry[p].y = solution[i][2];
        Sandbox::portState().common.pieceAry[p].rot = solution[i][3];
        Sandbox::portState().puzzle.pieceOrder[i] = p;
    }
    Sandbox::portState().puzzle.toDrawAll = true;
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

int main(int argc, char** argv) {
    const char* folder = argc > 1 ? argv[1] : nullptr;
    Canvas canvas; InputDriver input;
    ChiemagariApp &app = requireApp<ChiemagariApp>();

    app.onMount(&canvas, &input);
    // Original two-second OBN logo animation, then the title/menu.
    frames(app, 60);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_LOGO);
    image(folder, "chiemagari-logo", Sandbox::portState().common.arduboy);
    frames(app, 80);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_MENU);
    assert(Sandbox::Menu::portState().state == Sandbox::Menu::STATE_TITLE);
    assert(Sandbox::Menu::portState().menuPos == 0);
    assert(Sandbox::portState().common.recordState == Sandbox::RECORD_INITIAL);
    image(folder, "chiemagari-title", Sandbox::portState().common.arduboy);

    // B starts PLAY PUZZLE (first item); the pieces are the upstream scatter.
    tap(app, 1U << 4);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_PUZZLE);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_FREE);
    assert(Sandbox::portState().puzzle.cursorX == 8 && Sandbox::portState().puzzle.cursorY == 4);
    image(folder, "chiemagari-puzzle", Sandbox::portState().common.arduboy);

    // Real tilt movement through handleDPad.
    const int8_t startX = Sandbox::portState().puzzle.cursorX;
    g_ax = 0.5f; frames(app, 20); level();
    assert(Sandbox::portState().puzzle.cursorX > startX);
    const int8_t afterX = Sandbox::portState().puzzle.cursorY;
    g_ay = 0.5f; frames(app, 12); level();
    assert(Sandbox::portState().puzzle.cursorY > afterX);

    // Focus a piece under the cursor and pick it with B.
    int boardX = -1, boardY = -1;
    for (int y = 0; y < 9 && boardX < 0; ++y)
        for (int x = 0; x < 17; ++x)
            if (Sandbox::portState().puzzle.board[y][x].idx >= 0) { boardX = x; boardY = y; break; }
    assert(boardX >= 0);
    Sandbox::portState().puzzle.cursorX = (int8_t) boardX;
    Sandbox::portState().puzzle.cursorY = (int8_t) boardY;
    Sandbox::focusPiece(Sandbox::portState().puzzle.cursorX, Sandbox::portState().puzzle.cursorY);
    const int8_t picked = Sandbox::portState().puzzle.focusPieceIdx;
    assert(picked >= 0);
    tap(app, 1U << 4);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_PICKED);
    assert(Sandbox::portState().puzzle.focusPieceIdx == picked);

    // A + fresh direction rotates the held piece.
    const uint8_t oldRot = Sandbox::portState().common.pieceAry[picked].rot;
    g_ax = 0.5f;
    frames(app, 1, 1U << 5);
    frames(app, 2);
    level();
    assert(Sandbox::portState().common.pieceAry[picked].rot != oldRot);

    // Tilt alone moves the held piece without rotating it.
    const int8_t oldPieceX = Sandbox::portState().common.pieceAry[picked].x;
    const uint8_t keptRot = Sandbox::portState().common.pieceAry[picked].rot;
    const bool moveLeft = oldPieceX > 8;
    g_ax = moveLeft ? -0.5f : 0.5f; frames(app, 20); level();
    assert(moveLeft ? Sandbox::portState().common.pieceAry[picked].x < oldPieceX
                    : Sandbox::portState().common.pieceAry[picked].x > oldPieceX);
    assert(Sandbox::portState().common.pieceAry[picked].rot == keptRot);

    // Releasing over the still-overlapping scatter is rejected by putPieces.
    tap(app, 1U << 4);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_FREE);
    assert(Sandbox::portState().common.isDirty);

    // A leaves the board for the menu; CONTINUE returns to the live board.
    tap(app, 1U << 5);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_MENU);
    assert(Sandbox::Menu::portState().state == Sandbox::Menu::STATE_PUZZLE);
    assert(Sandbox::Menu::portState().menuPos == 0);

    // SOUND is item 1 (play count is still zero, so no GALLERY item).
    tiltTap(app, 0, 0.5f);
    assert(Sandbox::Menu::portState().menuPos == 1);
    const bool soundBefore = Sandbox::portState().common.arduboy.audio.enabled();
    tap(app, 1U << 4);
    const bool soundAfter = Sandbox::portState().common.arduboy.audio.enabled();
    assert(soundAfter != soundBefore);
    assert(Sandbox::portState().common.isDirty);
    tiltTap(app, 0, -0.5f);
    assert(Sandbox::Menu::portState().menuPos == 0);
    tap(app, 1U << 4);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_PUZZLE);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_FREE);

    // Complete a real non-overlapping pattern: release registers a new one.
    seedSolvedPieces();
    assert(!Sandbox::putPieces());
    Sandbox::portState().puzzle.state = Sandbox::STATE_PICKED;
    Sandbox::portState().puzzle.focusPieceIdx = 0;
    Sandbox::portState().puzzle.toDrawAll = true;
    tap(app, 1U << 4);
    assert(Sandbox::portState().common.clearCount == 1);
    assert(Sandbox::portState().puzzle.lastPatternIdx == 0);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_CLEAR);
    assert(Sandbox::portState().puzzle.clearEffectCount > 0);
    assert(!Sandbox::portState().common.isDirty);
    assert(Sandbox::portState().common.arduboy.eeprom.read(864) != 0xFF); // record signature written
    image(folder, "chiemagari-new-pattern", Sandbox::portState().common.arduboy);
    frames(app, 130);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_FREE);
    image(folder, "chiemagari-solved", Sandbox::portState().common.arduboy);

    // The same arrangement is remembered as already found, not counted again.
    Sandbox::portState().puzzle.state = Sandbox::STATE_PICKED;
    Sandbox::portState().puzzle.focusPieceIdx = 0;
    tap(app, 1U << 4);
    assert(Sandbox::portState().common.clearCount == 1);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_FREE);

    // Encoded-pattern round trip through the original codec.
    Sandbox::CODE_T expected[10], stored[10];
    Sandbox::encodePieces(expected);
    Sandbox::readEncodedPieces(0, stored);
    assert(memcmp(expected, stored, sizeof(expected)) == 0);
    Sandbox::decodePieces(stored);
    for (int i = 0; i < 10; ++i) {
        assert(Sandbox::portState().common.pieceAry[i].x >= 0 && Sandbox::portState().common.pieceAry[i].x < 17);
        assert(Sandbox::portState().common.pieceAry[i].y >= 0 && Sandbox::portState().common.pieceAry[i].y < 9);
    }

    // GALLERY now exists as item 1 and redraws the stored pattern.
    tap(app, 1U << 5);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_MENU);
    assert(Sandbox::Menu::portState().menuPos == 0);
    tiltTap(app, 0, 0.5f); // DOWN -> GALLERY
    assert(Sandbox::Menu::portState().menuPos == 1);
    tap(app, 1U << 4);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_GALLERY);
    assert(Sandbox::Menu::portState().state == Sandbox::Menu::STATE_GALLERY);
    image(folder, "chiemagari-gallery", Sandbox::portState().common.arduboy);
    tap(app, 1U << 5);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_MENU);

    // Shared wrapper hold gesture exits; unmount detaches and clears.
    exitHold(app);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 200);
    bool cleared = true;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) cleared &= canvas.pixels[y][x] == 0;
    assert(cleared);

    // Reentry (fresh app object, as the launcher creates) restores the RAM-only
    // record/gallery and re-runs the title.
    delete &app;
    ChiemagariApp &reopened = requireApp<ChiemagariApp>();
    reopened.onMount(&canvas, &input);
    frames(reopened, 140);
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_MENU);
    assert(Sandbox::portState().common.clearCount == 1);
    assert(Sandbox::portState().common.arduboy.audio.enabled() == soundAfter);
    assert(Sandbox::Menu::portState().state == Sandbox::Menu::STATE_TITLE);
    assert(Sandbox::portState().common.playFrames > 0); // resumed from the saved record
    tap(reopened, 1U << 4); // PLAY PUZZLE
    assert(Sandbox::portState().mode.mode == Sandbox::MODE_PUZZLE);
    assert(Sandbox::portState().puzzle.state == Sandbox::STATE_FREE);
    assert(Sandbox::portState().puzzle.creepFrames < 60); // session-only timers restarted
    image(folder, "chiemagari-reentry", Sandbox::portState().common.arduboy);
    exitHold(reopened);
    assert(reopened.exitRequested());
    reopened.onUnmount();

    delete &reopened;

    puts("chiemagari: logo/title/start/move/rotate/pick/release/new-pattern/"
         "duplicate/gallery/codec/exit/reentry passed");
    return 0;
}
