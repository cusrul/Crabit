// Actual Reversi source + actual compat adapter; only hardware/NanoUI mocked.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define STATE_TITLE 0
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define FPS 60
#define PROGMEM
#define pgm_read_byte(x) (*((uint8_t *)(x)))
#define pgm_read_word(x) (*((uint16_t *)(x)))
#define pgm_read_ptr(x) (*((uint16_t *)(x)))
#ifdef bitToggle
#error "Arduino has no bitToggle macro"
#endif

#include "app/apps/games/reversi/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3 && FPS == 60,
              "legacy launcher macros must survive");
#undef STATE_TITLE

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace S = ReversiSandbox;
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
static void tap(Application& app, uint8_t raw) { frames(app, 1, raw); frames(app, 5); }
static void tiltTap(Application& app, float ax, float ay) {
    g_ax = ax; g_ay = ay; frames(app, 1); level(); frames(app, 5);
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
    ReversiApp &app = requireApp<ReversiApp>();
    app.onMount(&canvas, &input);
    frames(app, 130);
    assert(S::portState().mode.mode == S::MODE_TITLE);
    assert(S::Title::portState().state == S::Title::STATE_TITLE);
    image(folder, "reversi-title", S::portState().common.arduboy);

    // Title menu: AS BLACK -> LEVEL 2 -> START (B on each menu item).
    tap(app, 1U << 4);
    assert(S::Title::portState().state == S::Title::STATE_TITLE);
    assert(S::getMenuItemCount() == 4);
    tap(app, 1U << 4);
    assert(S::portState().mode.mode == S::MODE_GAME);
    // Original opening animation places the four initial stones.
    frames(app, 150);
    assert(S::portState().game.state == S::STATE_PLAYING);
    assert(S::portState().game.board.numStones == 4);
    assert(S::portState().common.record.playCount == 1);
    image(folder, "reversi-start", S::portState().common.arduboy);

    // Tilt cursor movement then a real legal B placement.
    const uint8_t oldX = (uint8_t) S::portState().game.cursorPos.x;
    tiltTap(app, 0.5f, 0);
    assert(S::portState().game.cursorPos.x != oldX);
    int px = -1, py = -1;
    for (int y = 0; y < 8 && px < 0; ++y)
        for (int x = 0; x < 8; ++x)
            if ((S::portState().game.board.flag[y] & (1 << x)) &&
                    !(S::portState().game.board.black[y] & (1 << x)) && !(S::portState().game.board.white[y] & (1 << x))) {
                px = x; py = y; break;
            }
    assert(px >= 0);
    S::portState().game.cursorPos.x = (uint8_t) px;
    S::portState().game.cursorPos.y = (uint8_t) py;
    tap(app, 1U << 4);
    frames(app, 200);
    assert(S::portState().game.state == S::STATE_PLAYING);
    assert(S::portState().game.board.numStones > 4);
    assert(S::portState().common.record.playFrames > 0);
    assert(S::portState().common.record.gameMode == S::GAME_MODE_BLACK);
    image(folder, "reversi-play", S::portState().common.arduboy);

    // Occupied point is not placeable: no counters change.
    int bx = -1, by = -1;
    for (int y = 0; y < 8 && bx < 0; ++y)
        for (int x = 0; x < 8; ++x)
            if (S::portState().game.board.black[y] & (1 << x)) { bx = x; by = y; break; }
    assert(bx >= 0);
    const int beforeStones = S::portState().game.board.numStones;
    S::portState().game.cursorPos.x = (uint8_t) bx;
    S::portState().game.cursorPos.y = (uint8_t) by;
    tap(app, 1U << 4);
    assert(S::portState().game.board.numStones == beforeStones);

    // A opens the in-game menu; CONTINUE returns to play.
    tap(app, 1U << 5);
    assert(S::portState().game.state == S::STATE_MENU);
    tap(app, 1U << 4);
    assert(S::portState().game.state == S::STATE_PLAYING);

    // Full-board pass -> real finalize/win path, record written.
    memset(S::portState().game.board.black, 0xFF, sizeof(S::portState().game.board.black));
    memset(S::portState().game.board.white, 0, sizeof(S::portState().game.board.white));
    S::portState().game.board.isWhiteTurn = false;
    S::portState().game.board.isLastPassed = false;
    S::portState().common.record.gameMode = S::GAME_MODE_2PLAYERS;
    S::portState().game.board.numPlaceable = 0;
    S::portState().game.state = S::STATE_PLAYING;
    tap(app, 1U << 4); // pass 1
    tap(app, 1U << 4); // pass 2 -> double pass finalizes
    assert(S::portState().game.state == S::STATE_OVER);
    assert(S::portState().game.resultLabel != nullptr);
    assert(strcmp(reinterpret_cast<const char *>(S::portState().game.resultLabel), "BLACK WINS") == 0);
    frames(app, 2);
    image(folder, "reversi-result", S::portState().common.arduboy);
    const uint16_t savedCount = S::portState().common.record.playCount;
    const uint32_t savedFrames = S::portState().common.record.playFrames;

    exitHold(app);
    assert(app.exitRequested());
    app.onUnmount();

    delete &app;
    ReversiApp &reopened = requireApp<ReversiApp>();
    reopened.onMount(&canvas, &input);
    frames(reopened, 130);
    assert(S::portState().common.record.playCount == savedCount);
    assert(S::portState().common.record.playFrames == savedFrames);
    assert(S::Title::portState().state == S::Title::STATE_TITLE);
    image(folder, "reversi-reentry", S::portState().common.arduboy);
    exitHold(reopened);
    assert(reopened.exitRequested());
    reopened.onUnmount();

    delete &reopened;

    puts("reversi: title/start/tilt/place/illegal/menu/over/record/exit/reentry passed");
    return 0;
}
