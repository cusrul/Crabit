// Host-only runtime for the real Evasion port. Production header order/shape is
// emulated: real NanoUI geometry, Arduino bit macros without bitToggle, and the
// Realtek 16-bit pgm_read_ptr. CastleBoy's leaked state macro is defined too.
#include "NanoUI/compatibility/geometry.h"
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define memcpy_P memcpy
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define STATE_TITLE 0
#define STATE_GAME_OVER 3

#include "app/apps/games/evasion/app.h"
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
static void frame(EvasionApp &app, int n) {
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
    EvasionApp &app = requireApp<EvasionApp>();
    app.onMount(&canvas, &input);
    frame(app, 200);
    assert(EvasionSandbox::portState().mode.mode == EvasionSandbox::MODE_TITLE);
    assert(nonEmpty(canvas));
    pbm(canvas, dir, "evasion-title");

    EvasionSandbox::portState().common.record.simpleMode = 0;
    EvasionSandbox::portState().common.record.speed = 0;
    EvasionSandbox::portState().common.record.acceleration = 0;
    EvasionSandbox::portState().common.record.density = 1;
    EvasionSandbox::portState().common.record.thickness = 1;
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 2);
    assert(EvasionSandbox::portState().mode.mode == EvasionSandbox::MODE_GAME);

    frame(app, 70); // start countdown
    AmebaStore::imu_ax = 0.6f;
    frame(app, 25);
    assert(EvasionSandbox::Game::portState().state == EvasionSandbox::Game::STATE_PLAYING);
    assert(EvasionSandbox::Game::portState().playerVx == 1);
    assert(EvasionSandbox::Game::portState().score > 0);
    assert(nonEmpty(canvas));
    pbm(canvas, dir, "evasion-playing");

    // Pause with A, then B confirms CONTINUE.
    AmebaStore::raw_btn_mask = 1 << 5;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 2);
    assert(EvasionSandbox::Game::portState().state == EvasionSandbox::Game::STATE_MENU);
    AmebaStore::raw_btn_mask = 1 << 4;
    frame(app, 2);
    AmebaStore::raw_btn_mask = 0;
    frame(app, 2);
    assert(EvasionSandbox::Game::portState().state == EvasionSandbox::Game::STATE_PLAYING);

    // A duplicate launch of the same port id is refused before anything
    // is allocated; the live State keeps its engine and save slot.
    assert(CrabitArduboy::tryCreate<EvasionApp>() == nullptr);

    AmebaStore::imu_ax = 0;
    app.onUnmount();
    assert(!nonEmpty(canvas));
    app.onMount(&canvas, &input);
    assert(EvasionSandbox::portState().mode.mode == EvasionSandbox::MODE_LOGO);
    assert(EvasionSandbox::Title::portState().state == EvasionSandbox::Title::STATE_INIT);
    assert(EvasionSandbox::Game::portState().score == 0);
    frame(app, 200);
    assert(EvasionSandbox::portState().mode.mode == EvasionSandbox::MODE_TITLE);
    frame(app, 2000); // bounded idle
    // PAT hold exits through the shared adapter. Checked last: an exit
    // request is terminal for that Application instance.
    app.onUnmount();
    delete &app;
    {
        EvasionApp *q = CrabitArduboy::tryCreate<EvasionApp>();
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

    printf("evasion runtime passed\n");
    return 0;
}
