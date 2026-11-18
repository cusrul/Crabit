// Host-only runtime for the actual full Hopper port. Only the device boundary
// (NanoUI canvas/input/millis and Arduino millis/random helpers) is faked; the
// upstream gameplay, title, logo and record code is the real ported source.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define memcpy_P memcpy

// Production header shape: Castle Boy macros leak before Hopper, and Hollow's
// RECORD_* macros are pushed/restored by the port boundary.
#define FPS 60
#define STATE_TITLE 0
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_HELP 8
#define RECORD_NOT_READ 55
#define RECORD_INITIAL 56
#define RECORD_STORED 57

#include "app/apps/games/hopper/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3,
              "Castle Boy macros must be restored");
static_assert(RECORD_NOT_READ == 55 && RECORD_STORED == 57,
              "Hollow record macros must be restored");
#undef RECORD_NOT_READ
#undef RECORD_INITIAL
#undef RECORD_STORED

#if defined(APP_TITLE) || defined(APP_CODE) || defined(APP_VERSION) || \
    defined(APP_RELEASED) || defined(FLOORS_LEVEL) || defined(CHIPS) || \
    defined(SIGNAL_PTN)
#error "Hopper private macros leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}

namespace hs = HopperSandbox;

static float g_ax = 0, g_ay = 0, g_az = 1;

static void frames(AppHopper &app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax;
    AmebaStore::imu_ay = g_ay;
    AmebaStore::imu_az = g_az;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
        assert(!app.exitRequested());
    }
}

static void tap(AppHopper &app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 4);
}

static void pulseDirection(AppHopper &app, float ax, float ay) {
    g_ax = ax; g_ay = ay;
    frames(app, 2);
    g_ax = g_ay = 0;
    frames(app, 3);
}

static bool nonEmpty(const Canvas &canvas) {
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x)
            if (canvas.pixels[y][x]) return true;
    return false;
}

static void pbm(const Canvas &canvas, const char *folder, const char *name) {
    if (!folder) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.pbm", folder, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("P4\n128 64\n", file);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; x += 8) {
            uint8_t bits = 0;
            for (int b = 0; b < 8; ++b)
                if (canvas.pixels[y][x + b]) bits |= 1U << (7 - b);
            fputc(bits, file);
        }
    }
    assert(!fclose(file));
}

template <class App>
static App &requireApp() {
    App *app = CrabitArduboy::tryCreate<App>();
    assert(app && "tryCreate must prepare the port resources");
    return *app;
}

int main(int argc, char **argv) {
    const char *folder = argc > 1 ? argv[1] : nullptr;
    Canvas canvas;
    InputDriver input;

    assert(sizeof(hs::State) < 4096); // Bounded; no multi-KiB static game object.
    CrabitArduboy::releaseRetainedSaves();
    const CrabitArduboy::detail::Stats &stats = CrabitArduboy::allocationStats();

    AppHopper &app = requireApp<AppHopper>();
    assert(CrabitArduboy::tryCreate<AppHopper>() == nullptr); // one live instance
    assert(stats.live == 2);                                  // State + save slot
    assert(stats.live_bytes == sizeof(hs::State) + 1024);
    app.onMount(&canvas, &input);
    assert(hs::portState().mode.mode == hs::LOGO_MODE);
    assert(hs::portState().logo.counter == 120);
    frames(app, 60);
    assert(hs::portState().mode.mode == hs::LOGO_MODE);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-logo");
    frames(app, 70);
    assert(hs::portState().mode.mode == hs::TITLE_MODE);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-title");

    // Original title menu: SOUND toggles the RAM flag, RECORD opens BEST 10.
    pulseDirection(app, 0, 0.5f); // START -> SOUND
    assert(hs::portState().title.menuPos == hs::Title::MENU_SOUND);
    tap(app, 1U << 4);
    assert(hs::portState().title.sound == false);
    tap(app, 1U << 4);
    assert(hs::portState().title.sound == true);
    pulseDirection(app, 0, 0.5f); // SOUND -> RECORD
    assert(hs::portState().title.menuPos == hs::Title::MENU_RECORD);
    tap(app, 1U << 4);
    assert(hs::portState().title.state == hs::Title::STATE_RECORD);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-record");
    tap(app, 1U << 5); // back to menu
    assert(hs::portState().title.state == hs::Title::STATE_MENU);

    // CREDIT page keeps the upstream attribution screen.
    pulseDirection(app, 0, 0.5f); // RECORD -> CREDIT
    assert(hs::portState().title.menuPos == hs::Title::MENU_CREDIT);
    tap(app, 1U << 4);
    assert(hs::portState().title.state == hs::Title::STATE_CREDIT);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-credit");
    tap(app, 1U << 4);
    assert(hs::portState().title.state == hs::Title::STATE_MENU);

    // START GAME: CREDIT wraps to START.
    pulseDirection(app, 0, 0.5f);
    assert(hs::portState().title.menuPos == hs::Title::MENU_START);
    tap(app, 1U << 4);
    assert(hs::portState().mode.mode == hs::GAME_MODE);
    assert(hs::portState().game.state == hs::Game::STATE_START);

    // The original first long fall lands on the 4,096-Z start floor.
    frames(app, 100);
    assert(hs::portState().game.state == hs::Game::STATE_GAME);
    assert(hs::portState().game.score == 0);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-play");

    // Tilt right uses the shared adapter d-pad and the original movePlayer().
    const int16_t x0 = hs::portState().game.playerX;
    g_ax = 0.5f;
    frames(app, 5);
    g_ax = 0;
    frames(app, 2);
    assert(hs::portState().game.playerX > x0);

    // Original pause/resume through A|B.
    hs::portState().game.ignoreCnt = 0;
    tap(app, 1U << 4);
    assert(hs::portState().game.state == hs::Game::STATE_PAUSE);
    tap(app, 1U << 4);
    assert(hs::portState().game.state == hs::Game::STATE_GAME);
    assert(hs::portState().game.ignoreCnt > 0);

    // Goal floor: CLEAR, then the 96-frame spring animation advances level.
    hs::HopperGameState &game = hs::portState().game;
    const unsigned levelBefore = game.level;
    hs::FLOOR &goal = game.floorAry[game.floorIdxFirst];
    goal.type = hs::Game::FLRTYPE_GOAL;
    goal.x = game.playerX;
    goal.y = game.playerY;
    goal.z = 1;
    goal.size = 64;
    game.playerJump = 0;
    game.ignoreCnt = 0;
    frames(app, 1);
    assert(game.state == hs::Game::STATE_CLEAR);
    assert(game.playerJump == 96);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-clear");
    frames(app, 96);
    assert(game.level == levelBefore + 1);
    frames(app, 120);
    assert(game.state == hs::Game::STATE_GAME || game.state == hs::Game::STATE_CLEAR);

    // Five platform types: the original type table includes NORMAL/MOVE/
    // VANISH/SMALL/GOAL. Fill the ten managed slots and force the real draw
    // path, including the patterned and blinking goal surfaces.
    const uint8_t types[] = {
        hs::Game::FLRTYPE_NORMAL, hs::Game::FLRTYPE_MOVE,
        hs::Game::FLRTYPE_VANISH, hs::Game::FLRTYPE_SMALL,
        hs::Game::FLRTYPE_GOAL};
    for (unsigned i = 0; i < sizeof(game.floorAry) / sizeof(game.floorAry[0]); ++i) {
        hs::FLOOR &f = game.floorAry[i];
        f.type = types[i % (sizeof(types) / sizeof(types[0]))];
        f.pos = 0;
        f.size = uint8_t(20 + i * 4);
        f.sizeBack = f.size;
        f.z = 256 + int16_t(i * 128);
        f.x = game.playerX + (int16_t(i) - 5) * 32;
        f.y = game.playerY;
        f.vx = f.vy = 0;
    }
    game.floorIdxFirst = 0;
    game.floorIdxLast = 0;
    game.state = hs::Game::STATE_GAME;
    frames(app, 1);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-platforms");

    // Real game-over path: writes the original BEST 10 record to the RAM save.
    for (unsigned i = 1; i < sizeof(game.floorAry) / sizeof(game.floorAry[0]); ++i)
        game.floorAry[i].type = hs::Game::FLRTYPE_NONE;
    hs::FLOOR &firstFloor = game.floorAry[game.floorIdxFirst];
    firstFloor.type = hs::Game::FLRTYPE_NORMAL;
    game.state = hs::Game::STATE_GAME;
    game.ignoreCnt = 0;
    game.score = 12345;
    firstFloor.z = -1;
    game.playerJump = -64;
    frames(app, 1);
    assert(game.state == hs::Game::STATE_OVER);
    assert(game.isHiscore);
    assert(game.counter == 480);
    assert(nonEmpty(canvas));
    pbm(canvas, folder, "hopper-full-over");
    const Arduboy2Compat::RamEeprom &eeprom = hs::portState().engine.arduboy.eeprom;
    assert(eeprom.read(800) == 0x4F && eeprom.read(801) == 0x42);
    assert(eeprom.read(802) == 0x4E && eeprom.read(803) == 0x02);
    assert(eeprom.read(804) == (12345 & 0xFF));
    assert(eeprom.read(805) == ((12345 >> 8) & 0xFF));

    // Eight-second over screen returns to the real title, which reads BEST 10.
    frames(app, 480);
    assert(hs::portState().mode.mode == hs::TITLE_MODE);
    assert(hs::portState().title.recordState == hs::Title::RECORD_STORED);
    assert(hs::portState().title.hiScore[0] == 12345);
    assert(hs::portState().title.playCount == 1);

    // Destroy/recreate: the runtime commits the RAM save, the next mount reads
    // the same BEST 10 (RAM only, no Flash persistence).
    app.onUnmount();
    delete &app;
    assert(stats.live == 1); // retained save slot only
    assert(stats.live_bytes == 1024);

    AppHopper &app2 = requireApp<AppHopper>();
    app2.onMount(&canvas, &input);
    assert(hs::portState().mode.mode == hs::LOGO_MODE);
    assert(hs::portState().game.state == hs::Game::STATE_START);
    assert(hs::portState().game.score == 0);
    frames(app2, 130);
    assert(hs::portState().mode.mode == hs::TITLE_MODE);
    assert(hs::portState().title.recordState == hs::Title::RECORD_STORED);
    assert(hs::portState().title.hiScore[0] == 12345);
    assert(hs::portState().title.playCount == 1);

    // Re-entry on the same Application after unmount resets non-save state.
    app2.onUnmount();
    app2.onMount(&canvas, &input);
    assert(hs::portState().mode.mode == hs::LOGO_MODE);
    assert(hs::portState().title.recordState == hs::Title::RECORD_NOT_READ);
    assert(hs::portState().title.hiScore[0] == 0);
    frames(app2, 130);
    assert(hs::portState().title.recordState == hs::Title::RECORD_STORED);
    assert(hs::portState().title.hiScore[0] == 12345);

    // PAT hold exits through the shared adapter. Checked last because an exit
    // request is terminal for that Application instance.
    app2.onUnmount();
    delete &app2;
    {
        AppHopper *q = CrabitArduboy::tryCreate<AppHopper>();
        assert(q);
        Canvas canvas2;
        InputDriver input2;
        q->onMount(&canvas2, &input2);
        AmebaStore::raw_btn_mask = 1U << 6;
        q->onUpdate(fakeMillis + 17);
        assert(q->exitRequested());
        q->onUnmount();
        AmebaStore::raw_btn_mask = 0;
        delete q;
    }
    CrabitArduboy::releaseRetainedSaves();
    assert(CrabitArduboy::allocationStats().live == 0);
    assert(CrabitArduboy::allocationStats().live_bytes == 0);

    puts("hopper full runtime passed");
    return 0;
}
