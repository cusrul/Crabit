// Host harness for the actual james-b-collins Asteroid Dodge source. Only
// Canvas, InputDriver, AmebaStore telemetry and Arduino timing/random are fake.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
// Real Realtek program-memory reads are 16-bit; this port must not use them for
// native data/function pointers.
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

// Adversarial Castle Boy-style macros that precede this sketch in a combined
// translation unit. The port must hide them while including its own source and
// restore the exact values afterwards.
#define STATE_TITLE 0x1111
#define STATE_PLAY 0x2222
#define PLAYER_SPEED 0x3333
#define SCREEN_W 0x4444
#define SCREEN_H 0x5555

#include "app/apps/games/asteroiddodge/app.h"
#if STATE_TITLE != 0x1111 || STATE_PLAY != 0x2222 || PLAYER_SPEED != 0x3333 || \
    SCREEN_W != 0x4444 || SCREEN_H != 0x5555
#error "Asteroid Dodge did not restore preceding macros"
#endif
#undef STATE_TITLE
#undef STATE_PLAY
#undef PLAYER_SPEED
#undef SCREEN_W
#undef SCREEN_H

#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

namespace AD = AsteroidDodgeSandbox;

static float g_ax = 0, g_ay = 0;
static void tilt(float ax, float ay) { g_ax = ax; g_ay = ay; }
static void level() { g_ax = g_ay = 0; }

static void frames(Application &app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax;
    AmebaStore::imu_ay = g_ay;
    AmebaStore::imu_az = 1.0f;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
    }
}
static void tap(Application &app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 4, 0);
}
static void move(Application &app, float ax, float ay) {
    tilt(ax, ay);
    frames(app, 1);
    level();
    frames(app, 1);
}
static void clearAsteroids() {
    for (uint8_t i = 0; i < AD::MAX_ASTEROIDS; ++i) AD::portState().game.asteroids[i].active = false;
}

// The split build attributes a game's read-only text by namespace, so these
// named arrays must carry the exact strings the screens print; swapping them
// back to anonymous literals would drop the port's readonly asset.
static void assertScreenText() {
    assert(strcmp(AD::titleText, "AsteroidDodge") == 0);
    assert(strcmp(AD::playLabel, "Play") == 0);
    assert(strcmp(AD::hiLabel, "Hi:") == 0);
    assert(strcmp(AD::aboutLabel, "About") == 0);
    assert(strcmp(AD::muteLabel, "Mute:") == 0);
    assert(strcmp(AD::byText, "by James Collins") == 0);
    assert(strcmp(AD::githubText, "github.com/jbc2001/") == 0);
    assert(strcmp(AD::projectText, "AsteroidDodge-Arduboy") == 0);
    assert(strcmp(AD::continueText, "press any to continue") == 0);
    assert(strcmp(AD::gameOverText, "GAME OVER") == 0);
    assert(strcmp(AD::scoreLabel, "Score:") == 0);
}

// The named array must still paint through the same engine call the screens
// use, not merely sit in read-only memory.
static void assertTextPaints(Canvas &canvas) {
    AD::engine().clear();
    AD::engine().setCursor(0, 0);
    AD::engine().print(AD::titleText);
    AD::engine().display();
    bool painted = false;
    for (int y = 0; y < 8 && !painted; ++y)
        for (int x = 0; x < 80; ++x)
            if (canvas.pixels[y][x]) { painted = true; break; }
    assert(painted);
}

static void playFrames(Application &app, unsigned count, bool keepSafe) {
    for (unsigned i = 0; i < count; ++i) {
        if (keepSafe) clearAsteroids();
        frames(app, 1);
    }
}
static bool nonEmpty(const Canvas &canvas) {
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x)
            if (canvas.pixels[y][x]) return true;
    return false;
}
static void savePbm(const Canvas &canvas, const char *dir, const char *name) {
    if (!dir) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.pbm", dir, name);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P1\n128 64\n");
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) fputc(canvas.pixels[y][x] ? '0' : '1', f);
        fputc('\n', f);
    }
    fclose(f);
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
    Point nanoPoint{2, 3};
    Rect nanoRect{0, 0, 4, 4};
    assert(nanoPoint.x == 2 && nanoRect.width() == 4);

    Canvas canvas;
    InputDriver input;
    AsteroidDodgeApp &app = requireApp<AsteroidDodgeApp>();
    app.onMount(&canvas, &input);
    frames(app, 3);
    assert(AD::portState().game.state == AD::STATE_TITLE);
    assert(AD::portState().game.highScore == 0);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "asteroiddodge-title");
    assertScreenText();

    // Real title menu navigation through tilt, then A starts the original game.
    move(app, 0.0f, 0.5f);
    assert(AD::portState().game.menuSelection == 1);
    move(app, 0.0f, -0.5f);
    assert(AD::portState().game.menuSelection == 0);
    tap(app, 1U << 4);
    assert(AD::portState().game.state == AD::STATE_PLAY);
    assert(AD::portState().game.lives == 3 && AD::portState().game.score == 0);

    // Real movement and the original screen clamps.
    AD::portState().game.invincTimer = 200;
    int16_t x0 = AD::portState().game.player.x, y0 = AD::portState().game.player.y;
    tilt(0.5f, 0.0f); frames(app, 4); level(); frames(app, 1); // right 4
    assert(AD::portState().game.player.x == x0 + 4 && AD::portState().game.player.y == y0);
    tilt(-0.5f, 0.0f); frames(app, 2); level(); frames(app, 1); // left 2
    assert(AD::portState().game.player.x == x0 + 2);
    tilt(0.0f, 0.5f); frames(app, 3); level(); frames(app, 1); // down 3
    assert(AD::portState().game.player.y == y0 + 3);
    tilt(0.0f, -0.5f); frames(app, 1); level(); frames(app, 1); // up 1
    assert(AD::portState().game.player.y == y0 + 2);
    AD::portState().game.player.x = 0;
    move(app, -0.5f, 0.0f);
    assert(AD::portState().game.player.x == 0);
    AD::portState().game.player.x = AD::SCREEN_W - AD::PLAYER_W;
    move(app, 0.5f, 0.0f);
    assert(AD::portState().game.player.x == AD::SCREEN_W - AD::PLAYER_W);

    // Real score: one second of survived frames is worth one point.
    AD::portState().game.frameCount = 0;
    AD::portState().game.score = 0;
    AD::portState().game.invincTimer = 200;
    playFrames(app, 60, true);
    assert(AD::portState().game.score == 1);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "asteroiddodge-play");

    // Real avoidance: hold right while invincible, then enable collisions with
    // a stationary asteroid left behind; no life is lost at the new position.
    clearAsteroids();
    AD::portState().game.state = AD::STATE_PLAY;
    AD::portState().game.lives = 3;
    AD::portState().game.invincTimer = 255;
    AD::portState().game.player.x = 10;
    AD::portState().game.player.y = 10;
    AD::portState().game.asteroids[0].active = true;
    AD::portState().game.asteroids[0].x = 10;
    AD::portState().game.asteroids[0].y = 10;
    AD::portState().game.asteroids[0].vx = 0;
    tilt(0.5f, 0.0f); frames(app, 5); level(); frames(app, 1);
    assert(AD::portState().game.lives == 3 && AD::portState().game.player.x == 15);
    AD::portState().game.invincTimer = 0;
    frames(app, 1);
    assert(AD::portState().game.lives == 3);

    // Real collision and life loss, then the original respawn invincibility.
    clearAsteroids();
    AD::portState().game.state = AD::STATE_PLAY;
    AD::portState().game.lives = 3;
    AD::portState().game.invincTimer = 0;
    AD::portState().game.player.x = 10;
    AD::portState().game.player.y = 10;
    AD::portState().game.asteroids[0].active = true;
    AD::portState().game.asteroids[0].x = 10;
    AD::portState().game.asteroids[0].y = 10;
    AD::portState().game.asteroids[0].vx = 0;
    frames(app, 1);
    assert(AD::portState().game.state == AD::STATE_DEAD && AD::portState().game.lives == 2 && AD::portState().game.deathTimer == 30);
    savePbm(canvas, dir, "asteroiddodge-hit");
    frames(app, 30);
    assert(AD::portState().game.state == AD::STATE_PLAY && AD::portState().game.invincTimer == 60);

    // Real loss: the final hit saves the EEPROM high score and reaches GAMEOVER.
    clearAsteroids();
    AD::portState().game.state = AD::STATE_PLAY;
    AD::portState().game.lives = 1;
    AD::portState().game.invincTimer = 0;
    AD::portState().game.frameCount = 7 * 60;
    AD::portState().game.score = 7;
    AD::portState().game.player.x = 20;
    AD::portState().game.player.y = 20;
    AD::portState().game.asteroids[0].active = true;
    AD::portState().game.asteroids[0].x = 20;
    AD::portState().game.asteroids[0].y = 20;
    AD::portState().game.asteroids[0].vx = 0;
    frames(app, 1);
    assert(AD::portState().game.state == AD::STATE_DEAD && AD::portState().game.lives == 0 && AD::portState().game.deathTimer == 90);
    assert(AD::portState().game.highScore == 7);
    assert(AD::portState().game.arduboy.eeprom.read(AD::EEPROM_HIGHSCORE_ADDR) == 7);
    assert(AD::portState().game.arduboy.eeprom.read(AD::EEPROM_HIGHSCORE_ADDR + 1) == 0);
    assert(AD::portState().game.arduboy.eeprom.read(AD::EEPROM_MAGIC_ADDR) == AD::EEPROM_MAGIC);
    frames(app, 91);
    assert(AD::portState().game.state == AD::STATE_GAMEOVER);
    savePbm(canvas, dir, "asteroiddodge-gameover");

    // "press any to continue" returns to the title; A starts a fresh game.
    tap(app, 1U << 4);
    assert(AD::portState().game.state == AD::STATE_TITLE);
    tap(app, 1U << 4);
    assert(AD::portState().game.state == AD::STATE_PLAY && AD::portState().game.lives == 3 && AD::portState().game.score == 0);
    savePbm(canvas, dir, "asteroiddodge-restart");

    // Re-entry is clean but keeps the RAM-EEPROM high score.
    app.onUnmount();
    app.onMount(&canvas, &input);
    frames(app, 2);
    assert(AD::portState().game.state == AD::STATE_TITLE);
    assert(AD::portState().game.highScore == 7);
    assert(AD::portState().game.lives == 3 && AD::portState().game.score == 0);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "asteroiddodge-reentry");

    // Last, because it deliberately paints into the framebuffer.
    assertTextPaints(canvas);

    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 17);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 0);
    puts("asteroiddodge actual game runtime passed");
    delete &app;
}
