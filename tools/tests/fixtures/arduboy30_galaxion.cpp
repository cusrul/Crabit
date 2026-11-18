// Actual Galaxion sources + actual adapter; only hardware/NanoUI are faked.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#define memcpy_P memcpy

// Realtek/Ameba core shape: these read only 16 bits. The port must not use
// them for function pointers or 32-bit data pointers.
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_ptr(addr)  (*(const unsigned short *)(addr))

// Castle Boy macros that precede this port in the combined sketch.
#define FPS 60
#define STATE_TITLE 0
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_HELP 8

#include "app/apps/games/galaxion/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3, "Castle macros");
static_assert(STATE_HELP == 8 && FPS == 60, "Castle macros");
#undef STATE_GAME_OVER
#undef STATE_TITLE

#if defined(FRAMES_IN_SECOND) || defined(SCREEN_X) || defined(SCREEN_W) || \
    defined(CONVOY_COLS) || defined(MAX_ENEMIES) || defined(CAPTURE_SCREEN)
#error "Galaxion macro leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}
namespace bs = GalaxionSandbox;

// Count live game-owned heap objects without touching the shared allocator.
static long g_live = 0;
void* operator new(size_t n) {
    void *p = malloc(n);
    if (p) ++g_live;
    return p;
}
void operator delete(void *p) noexcept {
    if (p) { --g_live; free(p); }
}

// The Application object itself now comes from CrabitArduboy::tryCreate;
// the fixture's operator new counts it, so subtract that one baseline.
static const long kAppHeapObjects = 1;

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

static void holdDirection(Application& app, float ax, float ay) {
    g_ax = ax; g_ay = ay;
    frames(app, 1);
    g_ax = g_ay = 0;
    frames(app, 2);
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
    GalaxionApp &app = requireApp<GalaxionApp>();

    app.onMount(&canvas, &input);
    assert(bs::portState().mGame.mState == bs::GAME_ST_LOGO);
    frames(app, 150);
    assert(bs::portState().mGame.mState == bs::GAME_ST_TITLE);
    image(folder, "galaxion-title", bs::portState().mArduboy);

    // Auto fire defaults ON, but the title is not a battle: no missile can be
    // allocated there even after a long idle run.
    assert(bs::autoFirePreference());
    frames(app, 120);
    assert(bs::portState().mGame.mShip.missile == NULL);

    // The title owns the setting: cursor row 2 toggles it with left/right.
    // A held tilt must flip it once, not every frame.
    holdDirection(app, 0, 0.5f);
    holdDirection(app, 0, 0.5f);
    assert(bs::portState().mGame.mCursor == 2);
    holdDirection(app, -0.5f, 0);
    assert(!bs::autoFirePreference());
    holdDirection(app, 0.5f, 0);
    assert(bs::autoFirePreference());
    // Leave it off for the manual-fire checks below.
    holdDirection(app, -0.5f, 0);
    assert(!bs::autoFirePreference());
    holdDirection(app, 0, -0.5f);
    holdDirection(app, 0, -0.5f);
    assert(bs::portState().mGame.mCursor == 0);

    // Practice mode is selected with left; A then starts the original game.
    holdDirection(app, -0.5f, 0);
    assert(bs::portState().mGame.mMode == bs::GAME_MODE_PRACTICE);
    tap(app, 1U << 4);
    assert(bs::portState().mGame.mState == bs::GAME_ST_START);
    assert(g_live == kAppHeapObjects);
    frames(app, 320);
    assert(bs::portState().mGame.mState == bs::GAME_ST_PLAY);
    assert(bs::portState().mGame.mNumShips == 2);
    assert(bs::portState().mGame.mConvoy.num_enemies == 36);
    image(folder, "galaxion-play", bs::portState().mArduboy);

    // Tilt/button movement and the real A-shot allocation/lifetime.
    const int shipX = bs::portState().mGame.mShip.pos.get_x();
    g_ax = 0.5f;
    frames(app, 4);
    g_ax = 0;
    frames(app, 2);
    assert(bs::portState().mGame.mShip.pos.get_x() > shipX);
    assert(bs::portState().mGame.mShip.missile == NULL);
    bs::portState().mGame.mShip.dont_shoot = 0;
    bs::portState().mGame.mShip.state = bs::SHIP_ST_READY;
    tap(app, 1U << 4);
    assert(bs::portState().mGame.mShip.missile != NULL);
    assert(g_live > 0);
    frames(app, 80);
    assert(bs::portState().mGame.mShip.missile == NULL); // shot left the top or hit

    // Aim a real practice shot at the original convoy grid and score a hit.
    bs::portState().mGame.mMode = bs::GAME_MODE_PRACTICE;
    bs::portState().mGame.mFrame = 1;
    bs::portState().mGame.mNextStage = 0;
    bs::portState().mGame.mShip.state = bs::SHIP_ST_READY;
    bs::portState().mGame.mShip.dont_shoot = 0;
    bs::portState().mGame.mShip.missile = NULL;
    const int row = 3, col = 1;
    const int chkX = bs::portState().mGame.mConvoy.x + col * 6 + 2;
    const int chkY = bs::portState().mGame.mConvoy.y + row * 5 + 2;
    bs::portState().mGame.mShip.pos.set(chkX - 2, chkY);
    const uint16_t scoreBefore = bs::portState().mGame.mScore;
    const int enemiesBefore = bs::portState().mGame.mConvoy.num_enemies;
    tap(app, 1U << 4);
    assert(bs::portState().mGame.mScore > scoreBefore);
    assert(bs::portState().mGame.mConvoy.num_enemies < enemiesBefore);
    image(folder, "galaxion-hit", bs::portState().mArduboy);

    // Enemy launch and trajectory through the original Convoy code.
    bs::portState().mGame.mConvoy.dont_attack = 0;
    assert(bs::portState().mGame.mConvoy.do_launch_alien(3, 2, 1) >= 0);
    bs::Enemy *enemy = NULL;
    for (int i = 0; i < 10; ++i) if (bs::portState().mGame.mConvoy.enemy[i]) { enemy = bs::portState().mGame.mConvoy.enemy[i]; break; }
    assert(enemy != NULL);
    const int oldX = enemy->pos.get_x();
    const int oldY = enemy->pos.get_y();
    frames(app, 1);
    assert(enemy->pos.get_x() != oldX || enemy->pos.get_y() != oldY);

    // Original ship destruction -> 5-second respawn -> game over -> A restart.
    bs::portState().mGame.mConvoy.dont_attack = 10000;
    bs::portState().mGame.mNumShips = 0;
    bs::portState().mGame.mNextStage = 0;
    bs::portState().mGame.mShip.state = bs::SHIP_ST_READY;
    bs::portState().mGame.destroy_ship();
    frames(app, 70);
    assert(bs::portState().mGame.mState == bs::GAME_ST_GAMEOVER);
    image(folder, "galaxion-game-over", bs::portState().mArduboy);
    tap(app, 1U << 4);
    assert(bs::portState().mGame.mState == bs::GAME_ST_START);
    assert(g_live == kAppHeapObjects);

    // Reentry must release every heap object and reset to the logo.
    frames(app, 320);
    assert(bs::portState().mGame.mState == bs::GAME_ST_PLAY);

    // Auto fire (default ON) launches the original single missile without any
    // PAT input, but only inside GAME_ST_PLAY; the game-over screen stays
    // manual and OFF restores the manual path exactly.
    bs::autoFirePreference() = true;
    bs::portState().mGame.mShip.missile = NULL;
    bs::portState().mGame.mShip.state = bs::SHIP_ST_READY;
    bs::portState().mGame.mShip.dont_shoot = 0;
    frames(app, 2);
    assert(bs::portState().mGame.mShip.missile != NULL);
    bs::portState().mGame.mShip.missile = NULL;

    bs::portState().mGame.mState = bs::GAME_ST_GAMEOVER;
    bs::portState().mGame.mShip.state = bs::SHIP_ST_READY;
    bs::portState().mGame.mShip.dont_shoot = 0;
    frames(app, 5);
    assert(bs::portState().mGame.mShip.missile == NULL);
    bs::portState().mGame.mState = bs::GAME_ST_PLAY;

    bs::autoFirePreference() = false;
    bs::portState().mGame.mShip.state = bs::SHIP_ST_READY;
    bs::portState().mGame.mShip.dont_shoot = 0;
    frames(app, 5);
    assert(bs::portState().mGame.mShip.missile == NULL);
    bs::autoFirePreference() = true;

    bs::portState().mGame.mConvoy.dont_attack = 0;
    assert(bs::portState().mGame.mConvoy.do_launch_alien(3, 3, 1) >= 0);
    assert(g_live > 0);
    app.onUnmount();
    assert(g_live == kAppHeapObjects);
    assert(bs::portState().mGame.mShip.missile == NULL);
    for (int i = 0; i < 10; ++i) assert(bs::portState().mGame.mConvoy.enemy[i] == NULL);
    assert(bs::portState().mGame.mConvoy.exp == NULL && bs::portState().mGame.mConvoy.bscore == NULL);

    app.onMount(&canvas, &input);
    assert(bs::portState().mGame.mState == bs::GAME_ST_LOGO);
    assert(bs::portState().mGame.mScore == 0 && bs::portState().mGame.mNumShips == 0);
    // The preference is a powered-session value: it survives re-entry, while
    // every transient fire field above was reset.
    assert(bs::autoFirePreference());
    assert(bs::portState().mGame.mShip.missile == NULL);
    frames(app, 60);
    assert(bs::portState().mGame.mShip.missile == NULL);

    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(g_live == kAppHeapObjects);
    assert(canvas.displays > 500);
    puts("galaxion: title/practice/play/move/fire/hit/enemy/gameover/reentry/exit/render passed");
    delete &app;
    assert(g_live == 0);
}
