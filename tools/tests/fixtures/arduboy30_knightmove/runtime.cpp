// Host-only runtime for the real Knight Move port under production header
// shape: real NanoUI geometry, Arduino bit macros without bitToggle, Realtek
// 16-bit pgm_read_ptr, and CastleBoy's leaked state macro.
#include "NanoUI/compatibility/geometry.h"
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define memcpy_P memcpy
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define STATE_TITLE 0
#define STATE_GAME_OVER 3

#include "app/apps/games/knightmove/app.h"
#include <assert.h>
#include <stdio.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
uint32_t imu_updated_at = 0;
bool imu_received = false;
}

static bool nonEmpty(const Canvas &c) {
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x) if (c.pixels[y][x]) return true;
    return false;
}
static void pbm(const Canvas &c, const char *dir, const char *name) {
    if (!dir) return;
    char path[512]; snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "wb"); assert(f); fprintf(f, "P1\n128 64\n");
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) fputc(c.pixels[y][x] ? '1' : '0', f);
        fputc('\n', f);
    }
    fclose(f);
}
static void frame(KnightMoveApp &app, int n) {
    for (int i = 0; i < n; ++i) { fakeMillis += 17; app.onUpdate(fakeMillis); }
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
    const char *dir = argc > 1 ? argv[1] : nullptr;
    Canvas canvas; InputDriver input;
    KnightMoveApp &app = requireApp<KnightMoveApp>();
    app.onMount(&canvas, &input);
    frame(app, 130);
    assert(KnightMoveSandbox::portState().mode.mode == KnightMoveSandbox::MODE_TITLE);
    assert(nonEmpty(canvas));
    pbm(canvas, dir, "knightmove-title");

    // Deterministic seed for the random starting square / direction.
    randomSeed(12345);
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 2);
    assert(KnightMoveSandbox::portState().mode.mode == KnightMoveSandbox::MODE_GAME);
    frame(app, 125); // 2 s start phase
    assert(KnightMoveSandbox::Game::portState().state == KnightMoveSandbox::Game::STATE_MOVING);

    // B enables fast rolling through the real gameplay path; tilt steers.
    AmebaStore::imu_ax = 0.6f;
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 20);
    assert(KnightMoveSandbox::Game::portState().score > 0);
    assert(KnightMoveSandbox::Game::portState().state == KnightMoveSandbox::Game::STATE_MOVING ||
           KnightMoveSandbox::Game::portState().state == KnightMoveSandbox::Game::STATE_LEVEL_UP);
    assert(nonEmpty(canvas));
    pbm(canvas, dir, "knightmove-playing");

    // A opens the pause menu; B continues.
    AmebaStore::raw_btn_mask = 1 << 5;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 2);
    assert(KnightMoveSandbox::Game::portState().isMenu);
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 2);
    assert(!KnightMoveSandbox::Game::portState().isMenu);

    // A duplicate launch of the same port id is refused before anything
    // is allocated; the live State keeps its engine and save slot.
    assert(CrabitArduboy::tryCreate<KnightMoveApp>() == nullptr);

    AmebaStore::imu_ax = 0;
    app.onUnmount();
    assert(!nonEmpty(canvas));
    app.onMount(&canvas, &input);
    assert(KnightMoveSandbox::portState().mode.mode == KnightMoveSandbox::MODE_LOGO);
    assert(KnightMoveSandbox::Title::portState().state == KnightMoveSandbox::Title::STATE_INIT);
    assert(KnightMoveSandbox::Game::portState().score == 0);
    assert(KnightMoveSandbox::Game::portState().isMenu == false);
    frame(app, 130);
    assert(KnightMoveSandbox::portState().mode.mode == KnightMoveSandbox::MODE_TITLE);
    frame(app, 2000);
    // PAT hold exits through the shared adapter. Checked last: an exit
    // request is terminal for that Application instance.
    app.onUnmount();
    delete &app;
    {
        KnightMoveApp *q = CrabitArduboy::tryCreate<KnightMoveApp>();
        assert(q);
        Canvas c2; InputDriver i2;
        q->onMount(&c2, &i2);
        AmebaStore::raw_btn_mask = 1 << 6;
        q->onUpdate(fakeMillis + 17);
        assert(q->exitRequested());
        q->onUnmount();
        AmebaStore::raw_btn_mask = 0;
        delete q;
    }

    printf("knightmove runtime passed\n");
    return 0;
}
