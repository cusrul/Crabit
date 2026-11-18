// Actual Toyokumono sources + actual adapter; only hardware/NanoUI are faked.
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

#include "app/apps/games/toyokumono/app.h"

static_assert(STATE_TITLE == 0 && STATE_PLAY == 2 && STATE_GAME_OVER == 3, "Castle macros");
static_assert(STATE_HELP == 8 && FPS == 60 && ALIGN_LEFT == 0 && ALIGN_RIGHT == 2, "legacy macros");
#undef STATE_GAME_OVER
#undef STATE_TITLE

#if defined(APP_TITLE) || defined(DECIMAL_BITS) || defined(DOTS_MAX) || \
    defined(CLOUD_LIFE_MAX) || defined(PLAYER_ACCEL) || defined(IMG_CLOUD_W) || \
    defined(SND_PRIO_CLOUD) || defined(clamp) || defined(circulate) || \
    defined(addWithLimit) || defined(callInitFunc)
#error "Toyokumono macro leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}
namespace tk = ToyokumonoSandbox;

// Original rule constants kept local because port.h undefines the game macros.
static const uint16_t PLANT_POWER_MAX_LOCAL = 360;
static const int RECORD_EEPROM_BASE = 568;

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

static uint32_t wetTotal() {
    uint32_t total = 0;
    for (int i = 0; i < 16; ++i) total += tk::Game::portState().grounds[i].wet;
    return total;
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
    ToyokumonoApp &app = requireApp<ToyokumonoApp>();

    static_assert(RECORD_EEPROM_BASE + 6 + sizeof(tk::RECORD_T) <= 1024,
                  "record stays inside the private RAM EEPROM");

    app.onMount(&canvas, &input);
    assert(tk::portState().mode.mode == tk::MODE_LOGO);
    // Upstream MyArduboy2::beginNoLogo() calls audio.begin(); Arduboy2Audio's
    // fresh 0xFF RAM EEPROM means the documented default is sound ON. The old
    // assertion described the retired silent stub, not upstream behavior.
    assert(tk::portState().common.ab.isAudioEnabled());
    frames(app, 250);
    assert(tk::portState().mode.mode == tk::MODE_TITLE);
    assert(tk::Title::portState().state == tk::Title::STATE_TOP);
    assert(tk::portState().common.record.playCount == 0);
    image(folder, "toyokumono-title", tk::portState().common.ab);

    // Real tilt: DOWN moves the original title menu to GAME START, then a
    // single tap (B) activates it.
    assert(tk::getMenuItemPos() == 0);
    frames(app, 1, 0, 0, 0.6f, 1.0f);
    frames(app, 2);
    assert(tk::getMenuItemPos() == 1);
    tap(app, 1U << 4);
    frames(app, 4);
    assert(tk::portState().mode.mode == tk::MODE_GAME);
    assert(tk::Game::portState().state == tk::Game::STATE_START);
    image(folder, "toyokumono-start", tk::portState().common.ab);

    frames(app, 130);
    assert(tk::Game::portState().state == tk::Game::STATE_PLAYING);
    assert(tk::portState().common.record.playCount == 1);
    image(folder, "toyokumono-play", tk::portState().common.ab);

    // Real tilt movement: right acceleration changes the original player X.
    const int16_t startX = tk::Game::portState().playerX;
    frames(app, 4, 0, 0.8f, 0, 1.0f);
    frames(app, 2);
    assert(tk::Game::portState().playerX != startX);

    // B summons one cloud through the original edge-triggered path.
    const uint16_t elementsBefore = tk::Game::portState().elements;
    tap(app, 1U << 4);
    assert(tk::Game::portState().elements == elementsBefore - 1);
    bool hasCloud = false;
    for (int i = 0; i < 16; ++i) hasCloud = hasCloud || tk::Game::portState().clouds[i].life > 0;
    assert(hasCloud);

    // The cloud rains on the original sixteen grounds.
    const uint32_t wetBefore = wetTotal();
    frames(app, 70);
    assert(wetTotal() > wetBefore);

    // Injected legal plant state: wet middle band, full power, one frame of
    // life left. This exercises the original growth/life/scatter-score rule.
    tk::Game::portState().grounds[5].wet = 1800;   // wetLevel 3
    tk::Game::portState().grounds[5].growth = 2048;
    tk::Game::portState().grounds[5].life = 1;
    tk::Game::portState().grounds[5].power = PLANT_POWER_MAX_LOCAL;
    const uint16_t scoreBefore = tk::Game::portState().score;
    frames(app, 1);
    assert(tk::Game::portState().score > scoreBefore);
    image(folder, "toyokumono-play-grow", tk::portState().common.ab);

    // A opens the original pause menu; B on CONTINUE resumes.
    tap(app, 1U << 5);
    assert(tk::Game::portState().state == tk::Game::STATE_MENU);
    image(folder, "toyokumono-pause", tk::portState().common.ab);
    tap(app, 1U << 4);
    assert(tk::Game::portState().state == tk::Game::STATE_PLAYING);

    // Injected legal exhaustion: no elements, clouds or plants left. This is
    // the original game-over condition, not a shortcut around the rules.
    for (int i = 0; i < 16; ++i) tk::Game::portState().clouds[i].life = 0;
    for (int i = 0; i < 16; ++i) {
        tk::Game::portState().grounds[i].wet = 0;
        tk::Game::portState().grounds[i].growth = 0;
        tk::Game::portState().grounds[i].power = 0;
    }
    tk::Game::portState().elements = 0;
    frames(app, 1);
    assert(tk::Game::portState().state == tk::Game::STATE_OVER);
    const uint16_t best = tk::Game::portState().score;
    assert(best > 0);
    assert(tk::portState().common.record.hiscore[0] == best);
    image(folder, "toyokumono-over", tk::portState().common.ab);

    // Over -> B retries into a fresh original start state.
    frames(app, 70);
    tap(app, 1U << 4);
    assert(tk::Game::portState().state == tk::Game::STATE_START);
    assert(tk::Game::portState().score == 0);
    assert(tk::Game::portState().elements == 100);

    // Second over -> A quits to the title through the original handler.
    tk::Game::portState().state = tk::Game::STATE_PLAYING;
    tk::Game::portState().elements = 0;
    for (int i = 0; i < 16; ++i) tk::Game::portState().clouds[i].life = 0;
    frames(app, 1);
    assert(tk::Game::portState().state == tk::Game::STATE_OVER);
    frames(app, 70);
    tap(app, 1U << 5);
    assert(tk::Game::portState().state == tk::Game::STATE_LEAVE);
    frames(app, 2);
    assert(tk::portState().mode.mode == tk::MODE_TITLE);

    // Unmount/reentry resets the cursor/game state but keeps the RAM EEPROM
    // record, which is re-read by the original readRecord().
    app.onUnmount();
    app.onMount(&canvas, &input);
    assert(tk::portState().mode.mode == tk::MODE_LOGO);
    assert(tk::Game::portState().state == tk::Game::STATE_INIT);
    assert(tk::Game::portState().score == 0);
    assert(tk::portState().common.record.playCount > 0);
    assert(tk::portState().common.record.hiscore[0] == best);
    assert(tk::portState().common.recordState == tk::RECORD_STORED);
    assert(tk::portState().common.eepAddr <= RECORD_EEPROM_BASE + 6 + int(sizeof(tk::RECORD_T)));

    frames(app, 250);
    assert(tk::portState().mode.mode == tk::MODE_TITLE);
    assert(tk::Title::portState().state == tk::Title::STATE_TOP);

    // PAT hold (bit 6) is the shared exit gesture.
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 400);
    puts("toyokumono: title/menu/start/move/cloud/wet/grow/pause/over/retry/quit/reentry/exit/render passed");
    delete &app;
}
