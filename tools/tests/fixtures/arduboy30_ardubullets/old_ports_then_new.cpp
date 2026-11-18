// Production-order regression: real NanoUI geometry and Arduino-core macros are
// visible first, then the six accepted games, then every frozen port. Catalogue
// ports follow their generated registry index; minesweeper/game2048 are not in
// that catalogue yet, so they follow the catalogue-backed ports.
// The fake host NanoUI must not hide include-order, macro-leak or 16-bit
// pgm_read_ptr regressions.
#include "NanoUI/compatibility/geometry.h"

// Arduino.h defines these four macros but deliberately not bitToggle.
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
// Realtek's SDK Arduino.h supplies these two helpers; the host fixture mirrors
// that shape rather than claiming a self-contained Arduino implementation.
#define memcpy_P memcpy
// Exact Realtek AmebaD shape: native pointers are 32-bit, these read 16 bits.
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

#include "app/apps/games/castle_boy/app.h"
#include "app/apps/games/arduboy3d/app.h"
#include "app/apps/games/hopper/app.h"
#include "app/apps/games/hollow/app.h"
#include "app/apps/games/samegame/app.h"
#include "app/apps/games/lasers/app.h"
// Same order as the generated app/arduboy30_selected.h for --arduboy30 all.
#include "app/apps/games/ardubullets/app.h"
#include "app/apps/games/bananonsense/app.h"
#include "app/apps/games/chiemagari/app.h"
#include "app/apps/games/chribocchi/app.h"
#include "app/apps/games/evasion/app.h"
#include "app/apps/games/knightmove/app.h"
#include "app/apps/games/ardugo/app.h"
#include "app/apps/games/galaxion/app.h"
#include "app/apps/games/spacetanks/app.h"
#include "app/apps/games/minesweeper/app.h"
#include "app/apps/games/game2048/app.h"
#include "app/apps/games/stairssweep/app.h"
#include "app/apps/games/toyokumono/app.h"
#include "app/apps/games/rysk/app.h"
#include "app/apps/games/reversi/app.h"
#include "app/apps/games/quarto/app.h"
#include "app/apps/games/asteroiddodge/app.h"
#include "app/apps/games/memoryrhythm/app.h"
#include "app/apps/games/pong/app.h"
#include "app/apps/games/jumpboy/app.h"
#include "app/apps/games/longcat/app.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <type_traits>

// Every pilot keeps its own enum/type scope; the combined TU must not merge them.
static_assert(!std::is_same<ArdubulletsSandbox::MODE_T,
                            BananonsenseSandbox::MODE_T>::value, "MODE_T leaked");
static_assert(!std::is_same<BananonsenseSandbox::MODE_T,
                            ChiemagariSandbox::MODE_T>::value, "MODE_T leaked");
static_assert(!std::is_same<ChiemagariSandbox::MODE_T,
                            ChribocchiSandbox::MODE>::value, "MODE leaked");
static_assert(!std::is_same<ArdubulletsSandbox::MyArduboy2,
                            EvasionSandbox::MyArduboy2>::value,
              "identical helper headers must not share one class");

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}

// Catalogue ports are only launchable through CrabitArduboy::tryCreate<T>():
// the typed State (engine, framebuffer, game objects) and the retained save
// slot are allocated before the Application ever receives a canvas. A raw
// stack object would leave the sandbox pointers null, so the fixture uses the
// same entry point as the registry.
template <typename App>
static void runToTitle() {
    App *app = CrabitArduboy::tryCreate<App>();
    assert(app && "tryCreate must prepare the on-demand port state");
    Canvas canvas;
    InputDriver input;
    app->onMount(&canvas, &input);
    bool visible = false;
    for (int i = 0; i < 400; ++i) {
        fakeMillis += 17;
        app->onUpdate(fakeMillis);
        for (int y = 0; y < 64 && !visible; ++y)
            for (int x = 0; x < 128; ++x)
                if (canvas.pixels[y][x]) { visible = true; break; }
    }
    assert(visible);
    app->onUnmount();
    // Deleting the Application releases the port State exactly once and
    // commits the retained save slot before the engine disappears.
    delete app;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    const char *which = argv[1];
    uint8_t toggled = 0x10;
    CrabitArduboy::toggleBit(toggled, 4);
    assert(toggled == 0);
    assert(!CrabitArduboy::readBit(toggled, 4));
    assert(!Arduboy2Compat::collide(CrabitArduboy::Point(4, 3),
                                    CrabitArduboy::Rect(2, 2, 2, 2)));
    if (!strcmp(which, "ardubullets")) {
        runToTitle<ArdubulletsApp>();
    } else if (!strcmp(which, "bananonsense")) {
        runToTitle<BananonsenseApp>();
    } else if (!strcmp(which, "chiemagari")) {
        runToTitle<ChiemagariApp>();
    } else if (!strcmp(which, "chribocchi")) {
        runToTitle<ChribocchiApp>();
    } else if (!strcmp(which, "evasion")) {
        runToTitle<EvasionApp>();
    } else if (!strcmp(which, "knightmove")) {
        runToTitle<KnightMoveApp>();
    } else if (!strcmp(which, "ardugo")) {
        runToTitle<ArduGoApp>();
    } else if (!strcmp(which, "galaxion")) {
        runToTitle<GalaxionApp>();
    } else if (!strcmp(which, "spacetanks")) {
        runToTitle<SpaceTanksApp>();
    } else if (!strcmp(which, "minesweeper")) {
        runToTitle<MinesweeperApp>();
    } else if (!strcmp(which, "game2048")) {
        runToTitle<Game2048App>();
    } else if (!strcmp(which, "stairssweep")) {
        runToTitle<StairssweepApp>();
    } else if (!strcmp(which, "toyokumono")) {
        runToTitle<ToyokumonoApp>();
    } else if (!strcmp(which, "rysk")) {
        runToTitle<RyskApp>();
    } else if (!strcmp(which, "reversi")) {
        runToTitle<ReversiApp>();
    } else if (!strcmp(which, "quarto")) {
        runToTitle<QuartoApp>();
    } else if (!strcmp(which, "asteroiddodge")) {
        runToTitle<AsteroidDodgeApp>();
    } else if (!strcmp(which, "memoryrhythm")) {
        runToTitle<MemoryRhythmApp>();
    } else if (!strcmp(which, "pong")) {
        runToTitle<PongApp>();
    } else if (!strcmp(which, "jumpboy")) {
        runToTitle<JumpBoyApp>();
    } else if (!strcmp(which, "longcat")) {
        runToTitle<LongCatApp>();
    } else {
        return 2;
    }
    printf("%s combined runtime passed\n", which);
    return 0;
}
