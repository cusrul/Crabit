// Actual game sources + actual new adapters; only hardware/NanoUI are mocked.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <initializer_list>
#define bitRead(x,b) (((x) >> (b)) & 1UL)
#define bitSet(x,b) ((x) |= 1UL << (b))
#define bitClear(x,b) ((x) &= ~(1UL << (b)))
#define memcpy_P memcpy
// CastleBoy's existing global macros precede the new wrappers in launcher.h.
#define STATE_TITLE 0
#define FPS 60
#include "app/apps/games/hollow/app.h"
#include "app/apps/games/samegame/app.h"
#include "app/apps/games/lasers/app.h"
static_assert(STATE_TITLE == 0 && FPS == 60, "legacy game macros must survive new ports");

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

static float g_ax = 0, g_ay = 0, g_az = 1.0f;
static void tilt(float ax, float ay) { g_ax = ax; g_ay = ay; }
static void level() { g_ax = 0; g_ay = 0; }

static void frames(Application& app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax; AmebaStore::imu_ay = g_ay; AmebaStore::imu_az = g_az;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
        assert(!app.exitRequested());
    }
}
static void tap(Application& app, uint8_t raw) {
    frames(app, 1, raw); frames(app, 1);
}
static uint32_t digest(const uint8_t* bytes) {
    uint32_t h = 2166136261U;
    for (int i = 0; i < 1024; ++i) h = (h ^ bytes[i]) * 16777619U;
    return h;
}
static void image(const char* folder, const char* name, const Arduboy2Compat& engine) {
    assert(digest(engine.sBuffer) != digest(Arduboy2Compat().sBuffer));
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

int main(int argc, char** argv) {
    const char* folder = argc > 1 ? argv[1] : nullptr;
    Canvas canvas; InputDriver input;
    HollowSeekerApp hollow; SameGameApp same; LasersApp lasers;
    hollow.onMount(&canvas, &input); frames(hollow, 150);
    assert(HollowSeekerSandbox::mode == HollowSeekerSandbox::TITLE_MODE);
    image(folder, "hollow-title", HollowSeekerSandbox::arduboy);
    tap(hollow, 1U << 5); frames(hollow, 90);
    assert(HollowSeekerSandbox::mode == HollowSeekerSandbox::GAME_MODE);
    tilt(0.5f, 0); frames(hollow, 15); tilt(-0.5f, 0); frames(hollow, 15); level();
    assert(!HollowSeekerSandbox::isStart); // Original two-second ready countdown finished.
    image(folder, "hollow-play", HollowSeekerSandbox::arduboy);
    hollow.onUnmount();

    same.onMount(&canvas, &input); frames(same, 150);
    assert(SameGameSandbox::mode == SameGameSandbox::MODE_TITLE);
    image(folder, "samegame-title", SameGameSandbox::arduboy);
    tap(same, 1U << 4); frames(same, 10); // single tap = SameGame's B confirm
    assert(SameGameSandbox::mode == SameGameSandbox::MODE_GAME);
    image(folder, "samegame-play", SameGameSandbox::arduboy);
    // Exercise flood fill across the entire right boundary, original removal/undo.
    using namespace SameGameSandbox;
    memset(&Game::field, 0, sizeof(Game::field));
    Game::field.width = 20; Game::field.height = 10;
    for (int x = 0; x < 20; ++x) Game::field.objects[x] = 0x09249249U; // 10 tiles of colour 1.
    Game::field.cursorX = 19; Game::field.cursorY = 0;
    Game::checkLinkedObjects(); assert(Game::linkedCount == 200);
    Game::backupField(); Game::removeLinkedObjects();
    assert(Game::field.objects[19] == 0);
    Game::restoreField(); assert(Game::field.objects[19] == 0x09249249U);
    same.onUnmount();

    lasers.onMount(&canvas, &input); frames(lasers, 300);
    // The game retains its warning. Use actual buttons to accept, then start.
    assert(LasersSandbox::mode == LasersSandbox::MODE_WARN && LasersSandbox::y == 51);
    image(folder, "lasers-warning", LasersSandbox::arduboy);
    tap(lasers, 1U << 4); frames(lasers, 30);
    assert(LasersSandbox::mode == LasersSandbox::MODE_TITLE);
    image(folder, "lasers-title", LasersSandbox::arduboy);
    tap(lasers, 1U << 4); frames(lasers, 10);
    assert(LasersSandbox::mode == LasersSandbox::MODE_GAME);
    assert(LasersSandbox::state == LasersSandbox::STATE_PLAYING);
    const bool polarity = LasersSandbox::isPlayerWhite;
    tap(lasers, 1U << 4);
    assert(LasersSandbox::isPlayerWhite != polarity);
    tilt(0.5f, 0); frames(lasers, 15); tilt(-0.5f, 0); frames(lasers, 15); level();
    frames(lasers, 120);
    assert(LasersSandbox::laserCount > 0);
    image(folder, "lasers-play", LasersSandbox::arduboy);
    // Run beyond the first laser spawns and exercise original loss/retry paths.
    for (unsigned i = 0; i < 180; ++i) {
        tilt((i & 1) ? 0.5f : -0.5f, 0);
        frames(lasers, 10);
        tap(lasers, 1U << 4);
        assert(LasersSandbox::mode == LasersSandbox::MODE_GAME);
    }
    lasers.onUnmount();

    // Every app resets transient state on subsequent visits and detaches canvas.
    for (Application* app : {static_cast<Application*>(&hollow), static_cast<Application*>(&same), static_cast<Application*>(&lasers)}) {
        AmebaStore::raw_btn_mask = 0;
        app->onMount(&canvas, &input); frames(*app, 150);
        AmebaStore::raw_btn_mask = 1U << 6;
        app->onUpdate(fakeMillis);
        fakeMillis += 1000; app->onUpdate(fakeMillis);
        assert(app->exitRequested()); app->onUnmount();
    }
    assert(canvas.displays > 500);
    puts("three actual Arduboy games: title/play/input/reset/exit/render passed");
}
