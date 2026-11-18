// Actual Bananonsense sources + actual adapter; only hardware/NanoUI are faked.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define memcpy_P memcpy

// Reproduce the real Realtek/Ameba core shape: these read only 16 bits.
// The port must not use them for function pointers or 32-bit addresses.
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_ptr(addr)  (*(const unsigned short *)(addr))

// Reproduce the Castle Boy macros that precede this port in the combined TU.
#define FPS 60
#define STATE_TITLE 0
#define STATE_STAGE_INTRO 1
#define STATE_PLAY 2
#define STATE_GAME_OVER 3
#define STATE_GAME_FINISHED 4
#define STATE_LEVEL_FINISHED 5
#define STATE_STAGE_FINISHED 6
#define STATE_PLAYER_DIED 7
#define STATE_HELP 8

#include "app/apps/games/bananonsense/app.h"

static_assert(STATE_TITLE == 0, "Castle STATE_TITLE must survive the port");
static_assert(STATE_PLAY == 2, "Castle STATE_PLAY must survive the port");
static_assert(STATE_GAME_OVER == 3, "Castle STATE_GAME_OVER must survive the port");
static_assert(STATE_HELP == 8, "Castle STATE_HELP must survive the port");
static_assert(FPS == 60, "Castle FPS must survive the port");

// The game's own macros must not escape into the legacy TU.
#if defined(APP_TITLE) || defined(APP_CODE) || defined(FIELD_W) || \
    defined(IMG_LETTERS_W) || defined(IMG_GAME_OVER_W) || \
    defined(MENU_COUNT_MAX) || defined(COUNTER_MAX) || defined(TARGET_PATTERN) || \
    defined(SAYINGS_MAX) || defined(circulate) || defined(callDrawerFunc) || \
    defined(callInitFunc)
#error "Bananonsense macro leaked past port.h"
#endif

// The test itself may now use the private enum name; the restore above is the
// actual legacy-compatibility check.
#undef STATE_GAME_OVER
#undef STATE_TITLE

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

namespace bs = BananonsenseSandbox;

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

// PAT single = B, double = A for this port. The adapter stretches each tap to three frames.
static void tap(Application& app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 4);
}

static void tilt(float ax, float ay) { g_ax = ax; g_ay = ay; }
static void level() { g_ax = 0; g_ay = 0; }

static void holdDirection(Application& app, float ax, float ay) {
    tilt(ax, ay);
    frames(app, 1);
    level();
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

static void waitForPlaying(Application& app) {
    for (unsigned i = 0; i < 200 && bs::Game::portState().state != bs::Game::STATE_PLAYING; ++i)
        frames(app, 1);
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);
}

static void waitForMode(Application& app, bs::MODE_T mode, unsigned limit = 200) {
    for (unsigned i = 0; i < limit && bs::portState().mode.mode != mode; ++i) frames(app, 1);
    assert(bs::portState().mode.mode == mode);
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
    BananonsenseApp &app = requireApp<BananonsenseApp>();

    app.onMount(&canvas, &input);
    assert(bs::portState().mode.mode == bs::MODE_LOGO);
    frames(app, 40);
    assert(bs::portState().mode.mode == bs::MODE_LOGO);
    image(folder, "bananonsense-logo", bs::portState().common.ab);
    frames(app, 110);
    waitForMode(app, bs::MODE_TITLE);
    assert(bs::Title::portState().state == bs::Title::STATE_TOP);
    image(folder, "bananonsense-title", bs::portState().common.ab);

    // Credit screen exercises the original menu and packed credit text.
    holdDirection(app, 0, 0.5f); // down selects CREDIT
    tap(app, 1U << 4);           // B confirms
    assert(bs::Title::portState().state == bs::Title::STATE_CREDIT);
    frames(app, 2);
    image(folder, "bananonsense-credit", bs::portState().common.ab);
    tap(app, 1U << 5);           // A returns
    assert(bs::Title::portState().state == bs::Title::STATE_TOP);
    holdDirection(app, 0, -0.5f); // up selects GAME START again
    assert(bs::getMenuItemPos() == 0);

    tap(app, 1U << 4);           // GAME START
    waitForMode(app, bs::MODE_GAME);
    assert(bs::Game::portState().state == bs::Game::STATE_START);
    frames(app, 130);
    waitForPlaying(app);
    assert(bs::Game::isTarget(bs::Game::portState().targetX, bs::Game::portState().targetY));
    image(folder, "bananonsense-play", bs::portState().common.ab);

    // Real tilt movement changes the original cursor and scroll offsets.
    const int8_t startX = bs::Game::portState().cursorX;
    const int8_t startY = bs::Game::portState().cursorY;
    holdDirection(app, 0.5f, 0);
    assert(bs::Game::portState().cursorX == (startX + 1) % 16);
    holdDirection(app, 0, 0.5f);
    assert(bs::Game::portState().cursorY == (startY + 1) % 7);
    level();

    // The original target search also matches a BANANA run across the edge.
    {
        const int8_t row = bs::Game::portState().targetY;
        uint8_t savedLetter[6], savedTime[6];
        for (int i = 0; i < 6; ++i) {
            const int idx = (14 + i) % 16;
            savedLetter[i] = bs::Game::portState().field[row][idx].letter;
            savedTime[i] = bs::Game::portState().field[row][idx].time;
            bs::Game::portState().field[row][idx].letter = (0x998 >> (i * 2)) & 3;
            bs::Game::portState().field[row][idx].time = 0;
        }
        assert(bs::Game::isTarget(14, row));
        bs::Game::portState().field[row][14].letter = 2; // break the leading B
        assert(!bs::Game::isTarget(14, row));
        for (int i = 0; i < 6; ++i) {
            const int idx = (14 + i) % 16;
            bs::Game::portState().field[row][idx].letter = savedLetter[i];
            bs::Game::portState().field[row][idx].time = savedTime[i];
        }
        assert(bs::Game::isTarget(bs::Game::portState().targetX, bs::Game::portState().targetY));
    }

    // A wrong B costs the original three seconds and does not score.
    int8_t wrongX = bs::Game::portState().targetX;
    int8_t wrongY = bs::Game::portState().targetY;
    for (int d = 1; d < 16; ++d) {
        if (!bs::Game::isTarget((bs::Game::portState().targetX + d) % 16, bs::Game::portState().targetY)) {
            wrongX = (bs::Game::portState().targetX + d) % 16;
            break;
        }
    }
    bs::Game::portState().cursorX = wrongX;
    bs::Game::portState().cursorY = wrongY;
    const int16_t beforeMiss = bs::Game::portState().timer;
    tap(app, 1U << 4);
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);
    assert(bs::Game::portState().score == 0);
    assert(bs::Game::portState().timer <= beforeMiss - 60 * 3 + 2);

    // The guaranteed BANANA run is found through the real B action.
    bs::Game::portState().cursorX = bs::Game::portState().targetX;
    bs::Game::portState().cursorY = bs::Game::portState().targetY;
    tap(app, 1U << 4);
    assert(bs::Game::portState().state == bs::Game::STATE_FOUND);
    assert(bs::Game::portState().score == 1);
    assert(bs::portState().common.record.banana == 1);
    assert(bs::portState().common.isRecordDirty);
    image(folder, "bananonsense-found", bs::portState().common.ab);

    // Interlude rebuilds the field and shows one of the packed sayings.
    frames(app, 70);
    assert(bs::Game::portState().state == bs::Game::STATE_START);
    assert(bs::Game::portState().message[0] != '\0');
    bs::Game::setupMessage(0);
    assert(strcmp(bs::Game::portState().message,
                  "I BANANA BREAD TO THINK WHAT LIFE WOULD BE WITHOUT HUMOR.") == 0);
    bs::Game::setupMessage(47);
    assert(strcmp(bs::Game::portState().message, "ALL'S FAIR IN LOVE AND BANANAS.") == 0);
    frames(app, 130);
    waitForPlaying(app);

    // In-game menu: write the RAM record, continue, then leave to the title.
    tap(app, 1U << 5);
    assert(bs::Game::portState().state == bs::Game::STATE_MENU);
    frames(app, 2);
    image(folder, "bananonsense-menu", bs::portState().common.ab);
    assert(!bs::portState().common.isRecordDirty);
    tap(app, 1U << 4); // CONTINUE
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);
    tap(app, 1U << 5);
    assert(bs::Game::portState().state == bs::Game::STATE_MENU);
    holdDirection(app, 0, 0.5f); // BACK TO TITLE
    tap(app, 1U << 4);
    waitForMode(app, bs::MODE_TITLE);
    assert(bs::Title::portState().state == bs::Title::STATE_TOP);

    // Timeout / game-over / retry keep the original flow.
    tap(app, 1U << 4);
    waitForMode(app, bs::MODE_GAME);
    frames(app, 130);
    waitForPlaying(app);
    bs::Game::portState().timer = 2;
    frames(app, 4);
    assert(bs::Game::portState().state == bs::Game::STATE_TIME_UP);
    frames(app, 70);
    assert(bs::Game::portState().state == bs::Game::STATE_GAME_OVER);
    image(folder, "bananonsense-game-over", bs::portState().common.ab);
    tap(app, 1U << 4);
    assert(bs::Game::portState().state == bs::Game::STATE_START);
    assert(bs::Game::portState().score == 0);
    assert(bs::Game::isTarget(bs::Game::portState().targetX, bs::Game::portState().targetY));

    // Leave through the menu again, then reenter: transient state resets while
    // the RAM EEPROM keeps the saved banana count.
    frames(app, 130);
    waitForPlaying(app);
    tap(app, 1U << 5);
    holdDirection(app, 0, 0.5f);
    tap(app, 1U << 4);
    waitForMode(app, bs::MODE_TITLE);

    app.onUnmount();
    
    app.onMount(&canvas, &input);
    assert(bs::portState().mode.mode == bs::MODE_LOGO);
    assert(bs::Game::portState().state == bs::Game::STATE_START);
    assert(bs::Game::portState().score == 0);
    assert(bs::portState().common.record.banana == 1);
    frames(app, 150);
    waitForMode(app, bs::MODE_TITLE);
    image(folder, "bananonsense-reenter", bs::portState().common.ab);

    // Pinned exit gesture is still the adapter's job.
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 600);
    puts("bananonsense: title/credit/play/move/miss/find/sayings/menu/timeout/retry/reenter/exit/render passed");
    delete &app;
}
