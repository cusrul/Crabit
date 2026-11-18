// Actual RYSK source + actual compat adapter; only hardware/NanoUI mocked.
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
#define pgm_read_ptr(x) (*((uint16_t *)(x)))
#ifdef bitToggle
#error "Arduino has no bitToggle macro"
#endif

#include "app/apps/games/rysk/app.h"

static_assert(STATE_TITLE == 0 && STATE_GAME_OVER == 3 && FPS == 60,
              "legacy launcher macros must survive");

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace S = RyskSandbox;
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
    RyskApp &app = requireApp<RyskApp>();
    app.onMount(&canvas, &input);
    frames(app, 10);
    assert(!S::portState().common.isPlaying);
    image(folder, "rysk-title", S::portState().common.arduboy);

    tap(app, 1U << 4); // A starts the game (A|B edge in the original)
    assert(S::portState().common.isPlaying);
    assert(S::portState().common.score == 0);
    assert(S::portState().common.gameTime >= 60 * 60 && S::portState().common.gameTime <= 60 * 60 + 60 * 2);
    frames(app, 60 * 3);
    assert(S::portState().common.isPlaying);

    // Real tilt movement (continuous direction from the shared adapter).
    const int16_t startX = S::portState().common.playerX;
    g_ax = 0.5f; frames(app, 10); level();
    assert(S::portState().common.playerX > startX);

    // Deterministic pickup: place one original cookie on the player, then let
    // the real update() collect it and respawn it through newCookie().
    S::portState().common.cookie[0].x = S::portState().common.playerX;
    S::portState().common.cookie[0].y = S::portState().common.playerY;
    S::portState().common.cookie[0].vx = 0;
    S::portState().common.cookie[0].vy = 0;
    const int before = S::portState().common.score;
    frames(app, 1);
    assert(S::portState().common.score == before + 1);
    assert(!(S::portState().common.cookie[0].x == S::portState().common.playerX && S::portState().common.cookie[0].y == S::portState().common.playerY));
    image(folder, "rysk-play", S::portState().common.arduboy);

    // The original timer ends the session; A starts a fresh one.
    S::portState().common.gameTime = 1;
    frames(app, 60 + 3);
    assert(!S::portState().common.isPlaying);
    tap(app, 1U << 4);
    assert(S::portState().common.isPlaying && S::portState().common.score == 0 && S::portState().common.gameTime >= 60 * 60);

    exitHold(app);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 50);
    bool cleared = true;
    for (int y = 0; y < 64; ++y) for (int x = 0; x < 128; ++x) cleared &= canvas.pixels[y][x] == 0;
    assert(cleared);

    delete &app;
    RyskApp &reopened = requireApp<RyskApp>();
    reopened.onMount(&canvas, &input);
    frames(reopened, 10);
    assert(!S::portState().common.isPlaying && S::portState().common.score == 0 && S::portState().common.gameTime == 0);
    tap(reopened, 1U << 5); // B also starts, as upstream buttonDown(A|B)
    assert(S::portState().common.isPlaying);
    image(folder, "rysk-reentry", S::portState().common.arduboy);
    exitHold(reopened);
    assert(reopened.exitRequested());
    reopened.onUnmount();

    delete &reopened;

    puts("rysk: title/start/tilt/pickup/timer/restart/exit/reentry passed");
    return 0;
}
