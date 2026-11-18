// Actual Space Tanks sources + actual adapter; only hardware/NanoUI are faked.
#include <stdint.h>
#include <stdio.h>
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

#include "app/apps/games/spacetanks/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3, "Castle macros");
static_assert(STATE_HELP == 8 && FPS == 60, "Castle macros");
#undef STATE_GAME_OVER
#undef STATE_TITLE

#if defined(FRAMERATE) || defined(SCREEN_WIDTH) || defined(SCREEN_HEIGHT) || \
    defined(GAME_STATE_BANNER) || defined(GAME_STATE_PLAYING) || \
    defined(INTRO_MESSAGE_COUNT) || defined(MAX_FLASHING_MESSAGE_LENGTH)
#error "Space Tanks macro leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}
namespace bs = SpaceTanksSandbox;
static const uint8_t ST_BANNER = 0, ST_INTRO = 1, ST_PLAYING = 3, ST_DEAD = 4;

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

static void clearEnemies() {
    for (int i = 0; i < 20; ++i) bs::portState().game.spaceBats[i].isActive = false;
    bs::portState().game.currentHitSpaceBat = NULL;
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
    SpaceTanksApp &app = requireApp<SpaceTanksApp>();

    app.onMount(&canvas, &input);
    assert(bs::portState().state.gameState == ST_BANNER);
    frames(app, 150);
    assert(bs::portState().state.gameState == ST_BANNER);
    image(folder, "spacetanks-banner", bs::portState().engine.arduboy);

    // Auto fire defaults ON. The banner is the only non-battle surface, owns
    // the toggle (up/down edge) and never fires.
    assert(bs::autoFirePreference());
    assert(bs::portState().game.score == 0);
    holdDirection(app, 0, -0.5f);
    assert(!bs::autoFirePreference());
    holdDirection(app, 0, 0.5f);
    assert(bs::autoFirePreference());
    frames(app, 30);
    assert(bs::portState().game.score == 0);
    assert(bs::portState().game.currentShotCooldown == 0);
    // Leave it off for the manual-fire checks below.
    bs::autoFirePreference() = false;

    tap(app, 1U << 5); // B starts the intro
    frames(app, 65);
    assert(bs::portState().state.gameState == ST_INTRO);
    tap(app, 1U << 4); // A skips after the original one-second gate
    frames(app, 10);
    assert(bs::portState().state.gameState == ST_PLAYING);
    assert(bs::portState().game.score == 0);
    assert(bs::portState().game.player.spriteSizePx == 8);
    image(folder, "spacetanks-play", bs::portState().engine.arduboy);

    // Clear random enemies for deterministic movement and firing checks.
    clearEnemies();
    const uint8_t startX = bs::portState().game.player.X;
    const uint8_t startY = bs::portState().game.player.Y;
    holdDirection(app, 0.5f, 0);
    assert(bs::portState().game.player.X == uint8_t(startX + 1));
    holdDirection(app, 0, 0.5f);
    assert(bs::portState().game.player.Y == uint8_t(startY + 1));

    // Original shot path: place a regular bat in the laser line and fire B.
    bs::t_spaceBat &bat = bs::portState().game.spaceBats[0];
    bat = {0, 0, true, 0, 0, &bs::regularBatBoundingBox, 8, 0, 0, 1};
    bat.X = bs::portState().game.player.X + 20;
    bat.Y = bs::portState().game.player.Y;
    bs::portState().game.currentShotCooldown = 0;
    tap(app, 1U << 5);
    frames(app, 2);
    assert(bs::portState().game.currentHitSpaceBat != NULL);
    assert(bs::portState().game.score == 1);
    assert(bat.health == 0);
    image(folder, "spacetanks-hit", bs::portState().engine.arduboy);

    // Auto fire ON fires the original laser with no PAT input at all...
    bs::autoFirePreference() = true;
    clearEnemies();
    bs::t_spaceBat &autoBat = bs::portState().game.spaceBats[0];
    autoBat = {0, 0, true, 0, 0, &bs::regularBatBoundingBox, 8, 0, 0, 1};
    autoBat.X = bs::portState().game.player.X + 20;
    autoBat.Y = bs::portState().game.player.Y;
    bs::portState().game.currentShotCooldown = 0;
    const uint8_t autoScore = bs::portState().game.score;
    frames(app, 3);
    assert(bs::portState().game.score == uint8_t(autoScore + 1));
    assert(autoBat.health == 0);

    // ...while OFF restores the manual path: no cooldown starts by itself.
    bs::autoFirePreference() = false;
    clearEnemies();
    bs::t_spaceBat &offBat = bs::portState().game.spaceBats[0];
    offBat = {0, 0, true, 0, 0, &bs::regularBatBoundingBox, 8, 0, 0, 1};
    offBat.X = bs::portState().game.player.X + 20;
    offBat.Y = bs::portState().game.player.Y;
    bs::portState().game.currentShotCooldown = 0;
    frames(app, 40);
    assert(bs::portState().game.currentShotCooldown == 0);
    assert(bs::portState().game.score == uint8_t(autoScore + 1));
    assert(offBat.health == 1);

    // Path index must stay inside pathLength (the audited > length bug).
    clearEnemies();
    bs::portState().game.spaceBats[0].isActive = true;
    bs::portState().game.spaceBats[0].currentPathIndex = 0;
    bs::portState().game.spaceBats[0].currentPathStepIndex = 3;
    bs::portState().engine.arduboy.frameCount = 60;
    bs::advanceEnemies();
    assert(bs::portState().game.spaceBats[0].currentPathStepIndex == 0);

    // Real collision -> death animation -> 3-second flashing message -> banner.
    // Auto fire is ON here: the death sequence and banner must stay silent.
    bs::autoFirePreference() = true;
    clearEnemies();
    bs::portState().game.player.X = 10;
    bs::portState().game.player.Y = 10;
    bs::portState().game.spaceBats[0].isActive = true;
    bs::portState().game.spaceBats[0].X = 8;
    bs::portState().game.spaceBats[0].Y = 8;
    bs::calculatePlayerCollision();
    assert(bs::portState().game.player.deathAnimationFrame > 0);
    frames(app, 130);
    assert(bs::portState().game.player.isDead);
    assert(bs::portState().state.gameState == ST_DEAD);
    // Auto fire is still ON; the death screen is not a battle and fires nothing.
    const uint8_t deadScore = bs::portState().game.score;
    bs::portState().game.currentShotCooldown = 0;
    frames(app, 20);
    assert(bs::portState().game.currentShotCooldown == 0);
    assert(bs::portState().game.score == deadScore);
    for (int i = 0; i < 5 && (bs::portState().flash.flashingMessageFrame == 0 || (bs::portState().flash.flashingMessageFrame % 3) != 0); ++i)
        frames(app, 1);
    assert(bs::portState().flash.flashingMessageFrame > 0);
    image(folder, "spacetanks-dead-message", bs::portState().engine.arduboy);
    frames(app, 190);
    assert(bs::portState().state.gameState == ST_BANNER);
    image(folder, "spacetanks-reset-banner", bs::portState().engine.arduboy);
    // The banner has no fire path at all, even with auto fire still ON.
    const uint8_t bannerScore = bs::portState().game.score;
    bs::portState().game.spaceBats[0].isActive = true;
    bs::portState().game.spaceBats[0].X = bs::portState().game.player.X;
    bs::portState().game.spaceBats[0].Y = bs::portState().game.player.Y;
    frames(app, 30);
    assert(bs::portState().state.gameState == ST_BANNER);
    assert(bs::portState().game.score == bannerScore);

    // Reentry clears transient state and restores initial star positions.
    app.onUnmount();
    app.onMount(&canvas, &input);
    assert(bs::portState().state.gameState == ST_BANNER);
    assert(bs::autoFirePreference()); // powered-session preference survives
    assert(bs::portState().game.score == 0);
    assert(bs::portState().game.player.isDead == false);
    assert(bs::portState().game.currentHitSpaceBat == NULL);
    assert(bs::portState().game.movingStar[0][0] == 84);
    frames(app, 60);

    // Pinned adapter exit gesture.
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 300);
    puts("spacetanks: banner/intro/move/fire/collision/path/death/reentry/exit/render passed");
    delete &app;
}
