// Actual Stairs Sweep sources + actual adapter; only hardware/NanoUI are faked.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define memcpy_P memcpy

// Realtek/Ameba core shape: these read only 16 bits. The port must not use
// them for function pointers or 32-bit data pointers.
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_ptr(addr)  (*(const unsigned short *)(addr))

// Castle Boy and compatibility macros that precede this port.
#define FPS 60
#define STATE_TITLE 0
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_HELP 8

#include "app/apps/games/stairssweep/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3, "Castle macros");
static_assert(STATE_HELP == 8 && FPS == 60 && ALIGN_LEFT == 0 && ALIGN_RIGHT == 2, "legacy macros");
#undef STATE_GAME_OVER
#undef STATE_TITLE

#if defined(FIELD_W) || defined(FIELD_H) || defined(GAME_MODE_WAIT) || \
    defined(START_WAIT) || defined(ROTATE_DPAD) || defined(APP_TITLE)
#error "Stairs Sweep macro leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}
namespace bs = StairssweepSandbox;

static float g_ax = 0, g_ay = 0, g_az = 1.0f;

static void frames(Application& app, unsigned count, uint8_t raw = 0) {
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

static void tap(Application& app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 4);
}

static uint32_t digest(const uint8_t* bytes) {
    uint32_t h = 2166136261U;
    for (int i = 0; i < 1024; ++i) h = (h ^ bytes[i]) * 16777619U;
    return h;
}

static void image(const char* folder, const char* name, const Arduboy2Compat& engine) {
    assert(digest(engine.sBuffer) != digest(Arduboy2Compat().sBuffer));
    if (!folder) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.pbm", folder, name);
    FILE* file = fopen(path, "wb");
    assert(file);
    fputs("P4\n128 64\n", file);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; x += 8) {
            uint8_t bits = 0;
            for (int b = 0; b < 8; ++b)
                if (!(engine.sBuffer[y / 8 * 128 + x + b] & (1U << (y & 7))))
                    bits |= 1U << (7 - b);
            fputc(bits, file);
        }
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

int main(int argc, char** argv) {
    const char* folder = argc > 1 ? argv[1] : nullptr;
    Canvas canvas;
    InputDriver input;
    StairssweepApp &app = requireApp<StairssweepApp>();

    app.onMount(&canvas, &input);
    frames(app, 150);
    assert(bs::portState().mode.mode == bs::MODE_TITLE);
    assert(bs::Title::portState().state == bs::Title::STATE_TITLE);
    image(folder, "stairssweep-title", bs::portState().common.arduboy);

    tap(app, 1U << 4); // B confirms the default NOVICE entry
    frames(app, 200);
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);
    assert(bs::Game::portState().gameMode == bs::Game::GAME_MODE_CONTROL);
    image(folder, "stairssweep-play", bs::portState().common.arduboy);

    // Real tilt movement on the original rotated field.
    const int8_t startX = bs::Game::portState().bunchX;
    g_ay = -0.5f; // logical/right-up movement axis under ROTATE_DPAD
    frames(app, 1);
    g_ay = 0;
    frames(app, 2);
    assert(bs::Game::portState().bunchX != startX);

    // Logical UP + PAT B: rotate the current bunch (also tests the physical
    // screen-up alias added for the watch's standard tilt).
    bs::Game::portState().gameMode = bs::Game::GAME_MODE_CONTROL;
    bs::Game::portState().bunchR = 0;
    bs::Game::portState().bunchG = 64;
    bs::Game::portState().gameCounter = 30;
    g_ay = -0.5f;
    tap(app, 1U << 4);
    g_ay = 0;
    frames(app, 2);
    assert(bs::Game::portState().bunchR == 1);
    image(folder, "stairssweep-rotate", bs::portState().common.arduboy);

    // Star cancel path uses the same UP+B combination.
    bs::Game::portState().gameMode = bs::Game::GAME_MODE_STAR;
    bs::Game::portState().ballX = 2;
    bs::Game::portState().ballY = 3;
    bs::Game::portState().ballV = 0;
    bs::Game::portState().ballG = 56;
    bs::Game::portState().field[3][2] = bs::Game::OBJECT_STAR;
    g_ay = -0.5f;
    tap(app, 1U << 4);
    g_ay = 0;
    frames(app, 2);
    assert(bs::Game::portState().gameMode == bs::Game::GAME_MODE_FALL);
    assert(bs::Game::portState().field[3][2] == bs::Game::OBJECT_EMPTY);

    // A legal injected full line exercises the original erase/score rule.
    const uint32_t before = bs::Game::portState().score;
    for (int i = 0; i < 6; ++i) bs::Game::portState().field[0][i] = bs::Game::OBJECT_BOX;
    bs::Game::portState().state = bs::Game::STATE_PLAYING;
    bs::Game::portState().gameMode = bs::Game::GAME_MODE_FALL;
    bs::Game::portState().gameCounter = 0;
    frames(app, 40);
    assert(bs::Game::portState().score > before);
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);

    // Boundary rule: a bunch cannot leave the six-column field.
    assert(!bs::Game::isMoveable(-1, bs::Game::portState().bunchY, 0));
    assert(!bs::Game::isMoveable(6, bs::Game::portState().bunchY, 0));

    // Reentry resets the private cursor/game state while the RAM EEPROM record
    // remains available for the title's readRecord.
    app.onUnmount();
    app.onMount(&canvas, &input);
    assert(bs::portState().mode.mode == bs::MODE_LOGO);
    assert(bs::Game::portState().state == bs::Game::STATE_INIT);
    assert(bs::Game::portState().score == 0);
    assert(bs::Game::portState().bunchX == 0 && bs::Game::portState().bunchR == 0);
    frames(app, 60);

    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 300);
    puts("stairssweep: title/start/move/rotate/star/erase/bounds/reentry/exit/render passed");
    delete &app;
}
