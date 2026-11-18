// Host audio lifecycle across the real 27 registered ports (old six, 21
// catalogue ports, full Hopper). Only the device boundary is faked; each App
// is created through the production admission path and mounted/unmounted.
//
// This does not claim gameplay audio for every game (input-specific sounds are
// covered by each game's own suite). It proves the shared audio owner/flag/
// teardown contract is identical across all real wrappers and that a suspended
// App's repeated ticks cannot emit sound.
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Arduino PROGMEM helpers used by the real port sources on a host build.
#define memcpy_P memcpy
#define strcpy_P strcpy

#include "app/apps/games/castle_boy/app.h"
#include "app/apps/games/arduboy3d/app.h"
#include "app/apps/games/hopper/app.h"
#include "app/apps/games/hollow/app.h"
#include "app/apps/games/samegame/app.h"
#include "app/apps/games/lasers/app.h"
#include "app/apps/games/ardubullets/app.h"
#include "app/apps/games/bananonsense/app.h"
#include "app/apps/games/chiemagari/app.h"
#include "app/apps/games/chribocchi/app.h"
#include "app/apps/games/evasion/app.h"
#include "app/apps/games/knightmove/app.h"
#include "app/apps/games/ardugo/app.h"
#include "app/apps/games/minesweeper/app.h"
#include "app/apps/games/game2048/app.h"
#include "app/apps/games/spacetanks/app.h"
#include "app/apps/games/galaxion/app.h"
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

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}

namespace {

unsigned g_emitted = 0;

void capture(void *, uint16_t, uint16_t) { ++g_emitted; }

template <class App>
void checkPort(const char *name) {
    Canvas canvas;
    InputDriver input;
    App *app = CrabitArduboy::tryCreate<App>();
    assert(app && name);
    app->onMount(&canvas, &input);
    assert(CrabitArduboyAudio::hasOwner());
    assert(CrabitArduboyAudio::owner() == static_cast<void *>(app));
    // Every upstream port's fresh sound flag is ON (EEPROM 0xFF or explicit
    // begin). A per-port menu may turn it off later; that is game-tested.
    assert(CrabitArduboyAudio::enabled());

    fakeMillis += 17;
    app->onUpdate(fakeMillis);
    fakeMillis += 17;
    app->onUpdate(fakeMillis);

    app->onSuspend();
    assert(!CrabitArduboyAudio::hasOwner());
    const unsigned before = g_emitted;
    // Repeated ticks of a suspended/background App must not emit or re-claim
    // ownership; its gameplay code may still run.
    for (int i = 0; i < 6; ++i) {
        fakeMillis += 17;
        app->onUpdate(fakeMillis);
        assert(!CrabitArduboyAudio::hasOwner());
    }
    assert(g_emitted == before);

    app->onResume();
    assert(CrabitArduboyAudio::hasOwner());
    assert(CrabitArduboyAudio::enabled()); // resume keeps the current choice
    app->onUnmount();
    assert(!CrabitArduboyAudio::hasOwner());
    delete app;
}

// Suspending one App, mounting another, then resuming must restore the first
// App's own on/off choice instead of leaking the global flag. Calls go through
// Application* so the onSuspend/onResume overrides are exercised virtually.
void checkPerAppChoiceAcrossSuspend() {
    Canvas canvas;
    InputDriver input;

    ToyokumonoApp *a = CrabitArduboy::tryCreate<ToyokumonoApp>();
    assert(a);
    Application *pa = a;
    pa->onMount(&canvas, &input);
    assert(CrabitArduboyAudio::enabled()); // A's saved choice is ON
    pa->onSuspend();
    assert(!CrabitArduboyAudio::hasOwner());

    ChiemagariApp *b = CrabitArduboy::tryCreate<ChiemagariApp>();
    assert(b);
    Application *pb = b;
    pb->onMount(&canvas, &input);
    assert(CrabitArduboyAudio::enabled()); // fresh mount defaults ON
    CrabitArduboyAudio::setEnabled(false); // B mutes itself
    pb->onSuspend();

    pa->onResume();
    assert(CrabitArduboyAudio::enabled()); // A's saved ON, not B's OFF
    CrabitArduboyAudio::setEnabled(false);
    pa->onSuspend();

    pb->onResume();
    assert(!CrabitArduboyAudio::enabled()); // B's saved OFF
    assert(CrabitArduboyAudio::owner() == static_cast<void *>(b));

    pb->onUnmount();
    delete b;
    pa->onUnmount();
    delete a;
    assert(!CrabitArduboyAudio::hasOwner());
}

} // namespace

int main() {
    CrabitArduboyAudio::setSink(capture);
    checkPerAppChoiceAcrossSuspend();
    checkPort<CastleBoyApp>("castle_boy");
    checkPort<Arduboy3DApp>("arduboy3d");
    checkPort<AppHopper>("hopper");
    checkPort<HollowSeekerApp>("hollow");
    checkPort<SameGameApp>("samegame");
    checkPort<LasersApp>("lasers");
    checkPort<ArdubulletsApp>("ardubullets");
    checkPort<BananonsenseApp>("bananonsense");
    checkPort<ChiemagariApp>("chiemagari");
    checkPort<ChribocchiApp>("chribocchi");
    checkPort<EvasionApp>("evasion");
    checkPort<KnightMoveApp>("knightmove");
    checkPort<ArduGoApp>("ardugo");
    checkPort<MinesweeperApp>("minesweeper");
    checkPort<Game2048App>("game2048");
    checkPort<SpaceTanksApp>("spacetanks");
    checkPort<GalaxionApp>("galaxion");
    checkPort<StairssweepApp>("stairssweep");
    checkPort<ToyokumonoApp>("toyokumono");
    checkPort<RyskApp>("rysk");
    checkPort<ReversiApp>("reversi");
    checkPort<QuartoApp>("quarto");
    checkPort<AsteroidDodgeApp>("asteroiddodge");
    checkPort<MemoryRhythmApp>("memoryrhythm");
    checkPort<PongApp>("pong");
    checkPort<JumpBoyApp>("jumpboy");
    checkPort<LongCatApp>("longcat");
    printf("arduboy audio games passed (27 ports, %u events)\n", g_emitted);
    return 0;
}
