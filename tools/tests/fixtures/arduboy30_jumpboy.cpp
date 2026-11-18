// Actual JumpBoy sources + actual adapter; only hardware/NanoUI are faked.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define memcpy_P memcpy

// Realtek/Ameba core shape: these read only 16 bits. The port must not use
// them for function pointers or 32-bit data pointers.
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_ptr(addr)  (*(const unsigned short *)(addr))

// Macros that precede this port in the combined sketch, plus values that let
// the test prove port.h restores them after the game's own macros are gone.
#define FPS 60
#define STATE_TITLE 0
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define MODE_TITLE 77
#define NOTE_A1 1234

#include "app/apps/games/jumpboy/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3,
              "Castle macros");
static_assert(FPS == 60, "FPS restored");
static_assert(MODE_TITLE == 77, "MODE_TITLE restored");
// NOTE_A1 comes from this fixture; NOTE_C4 is restored to the shared
// ArduboyTones pitch table (262 Hz), not to a retired zero-valued stub.
static_assert(NOTE_A1 == 1234 && NOTE_C4 == 262, "note macros restored");

#if defined(MODE_START) || defined(MODE_RUNNING) || defined(MODE_GAMECLEAR) || \
    defined(MODE_GAMEOVER)
#error "JumpBoy macro leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1.0f;
}
namespace jb = JumpBoySandbox;

// Mode values after port.h has removed the game macros.
static const uint8_t MODE_TITLE_V = 1, MODE_START_V = 2, MODE_RUNNING_V = 3;
static const uint8_t MODE_CLEAR_V = 4, MODE_OVER_V = 5;

static void frames(Application& app, unsigned count, uint8_t raw = 0,
                   float ax = 0, float ay = 0, float az = 1.0f) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = ax;
    AmebaStore::imu_ay = ay;
    AmebaStore::imu_az = az;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
        assert(!app.exitRequested());
    }
}

static void tap(Application& app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 3);
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

static int springTotal() {
    int total = 0;
    for (int i = 0; i < 16; ++i) total += jb::pSprings[i];
    return total;
}

// Legal board state: a single spring far from the player's landing window.
// This is a reachable depletion state, used to reach the original miss branch.
static void isolateSpring() {
    int nextScroll = jb::portState().spring.pScroll - jb::portState().spring.pScrollSpeed;
    if (nextScroll < 0) nextScroll += 128;
    int chosen = -1;
    for (int i = 0; i < 16; ++i) {
        int x = nextScroll - i * 8;
        if (x < 0) x += 128;
        x += 4;
        if (x < jb::portState().game.pX - 4 || x > jb::portState().game.pX + 12) {
            chosen = i;
            break;
        }
    }
    assert(chosen >= 0);
    for (int i = 0; i < 16; ++i) jb::pSprings[i] = 0;
    jb::pSprings[chosen] = 4;
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
    JumpBoyApp &app = requireApp<JumpBoyApp>();

    app.onMount(&canvas, &input);
    assert(jb::portState().game.pMode == MODE_TITLE_V);
    assert(jb::portState().game.lives == 3 && jb::portState().game.pX == 20 && jb::portState().game.pY == 0 && jb::portState().game.record == 0);
    frames(app, 6);
    assert(jb::portState().game.pMode == MODE_TITLE_V);
    image(folder, "jumpboy-title", jb::portState().engine.arduboy);

    // A starts through the original title input path; bStart stays up for the
    // original two seconds (frame state instead of delay).
    tap(app, 1U << 4);
    frames(app, 2);
    assert(jb::portState().game.pMode == MODE_START_V);
    assert(jb::portState().game.startWait > 0 && jb::portState().game.lives == 3 && jb::portState().game.stage == 1);
    image(folder, "jumpboy-start", jb::portState().engine.arduboy);

    frames(app, 130);
    assert(jb::portState().game.pMode == MODE_RUNNING_V);
    assert(jb::portState().game.pY >= 24 && jb::portState().game.pY <= 46);
    image(folder, "jumpboy-running", jb::portState().engine.arduboy);

    // Real tilt movement through the original RIGHT_BUTTON direction case.
    const int startX = jb::portState().game.pX;
    frames(app, 12, 0, 0.8f, 0, 1.0f);
    frames(app, 2);
    assert(jb::portState().game.pX > startX);

    // Real spring collision: the ball falls, consumes one original spring and
    // reverses upwards (the original bounce rule).
    const int springsBefore = springTotal();
    frames(app, 22);
    assert(springTotal() == springsBefore - 1);
    assert(jb::portState().game.pYY < 0);
    image(folder, "jumpboy-bounce", jb::portState().engine.arduboy);

    // Real coin collision and score increment through _collisionCoins.
    for (int i = 0; i < 10; ++i) jb::portState().coinsState.coins[i].active = false;
    jb::portState().coinsState.coin.concurrent_coin_max = 1;
    const uint8_t qtyBefore = jb::portState().coinsState.coin.qty;
    jb::portState().coinsState.coins[0].active = true;
    jb::portState().coinsState.coins[0].x = jb::portState().game.pX;
    jb::portState().coinsState.coins[0].y = jb::portState().game.pY;
    jb::portState().coinsState.coins[0].spCount = 100;
    frames(app, 2);
    assert(jb::portState().coinsState.coin.qty == qtyBefore + 1);
    assert(!jb::portState().coinsState.coins[0].active);

    // Real stage clear: the last needed coin trips the original norma check.
    for (int i = 0; i < 10; ++i) jb::portState().coinsState.coins[i].active = false;
    jb::portState().coinsState.coin.concurrent_coin_max = 1;
    jb::portState().coinsState.coin.qty = jb::portState().coinsState.coin.norma - 1;
    jb::portState().coinsState.coins[0].active = true;
    jb::portState().coinsState.coins[0].x = jb::portState().game.pX;
    jb::portState().coinsState.coins[0].y = jb::portState().game.pY;
    jb::portState().coinsState.coins[0].spCount = 100;
    frames(app, 2);
    assert(jb::portState().game.pMode == MODE_CLEAR_V);
    assert(jb::portState().game.record == 1 && jb::portState().game.stage == 2);
    image(folder, "jumpboy-clear", jb::portState().engine.arduboy);

    frames(app, 130);
    assert(jb::portState().game.pMode == MODE_RUNNING_V);

    // Legal board depletion: all but one spring gone, and the surviving spring
    // is outside the landing window. The next landing is an original miss.
    jb::portState().game.pY = 46;
    isolateSpring();
    const uint8_t livesBefore = jb::portState().game.lives;
    frames(app, 2);
    assert(jb::portState().game.lives == livesBefore - 1);
    assert(jb::portState().game.pMode == MODE_RUNNING_V);
    assert(jb::portState().game.pWaitFlg && jb::portState().game.missHold > 0 && jb::portState().game.pASpeed == 0);
    frames(app, 190);
    assert(!jb::portState().game.pWaitFlg && jb::portState().game.missHold == 0);

    // Last life: the original game-over branch, then the original three-second
    // pause before returning to the title. record stays for this session.
    jb::portState().game.lives = 1;
    jb::portState().game.pY = 46;
    isolateSpring();
    frames(app, 2);
    assert(jb::portState().game.lives == 0);
    assert(jb::portState().game.pMode == MODE_OVER_V);
    frames(app, 190);
    assert(jb::portState().game.pMode == MODE_TITLE_V);
    assert(jb::portState().game.record == 1);
    frames(app, 4);
    image(folder, "jumpboy-title-record", jb::portState().engine.arduboy);

    // Unmount/reentry clears all transients, including the RAM-only record:
    // JumpBoy has no EEPROM save, so the record scope is one mount.
    app.onUnmount();
    app.onMount(&canvas, &input);
    assert(jb::portState().game.pMode == MODE_TITLE_V);
    assert(jb::portState().game.record == 0 && jb::portState().game.lives == 3 && jb::portState().game.stage == 1);
    assert(jb::portState().game.pX == 20 && jb::portState().game.pY == 0 && jb::portState().spring.pScroll == 128 && jb::portState().spring.pScrollSpeed == 1);
    assert(springTotal() == 41);
    assert(jb::portState().coinsState.coin.qty == 0 && jb::portState().coinsState.coin.spCount == 0);
    bool anyCoin = false;
    for (int i = 0; i < 10; ++i) anyCoin = anyCoin || jb::portState().coinsState.coins[i].active;
    assert(!anyCoin);
    assert(!jb::portState().powerups.powerup.active);

    frames(app, 6);
    assert(jb::portState().game.pMode == MODE_TITLE_V);

    // PAT hold (bit 6) is the shared exit gesture.
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 30);
    puts("jumpboy: title/start/tilt/spring/coin/clear/miss/over/reentry/exit/render passed");
    delete &app;
}
