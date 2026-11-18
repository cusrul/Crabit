// Host-only harness for the actual Chri-Bocchi Cat port. The only fakes are
// Arduino timing/random, the NanoUI Canvas/InputDriver boundary and AmebaStore
// telemetry; game logic, bitmaps, menu, records and the shared compat layer are
// compiled from their real sources.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <initializer_list>
#include "app/apps/games/chribocchi/app.h"

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 0;
}

static float g_ax = 0, g_ay = 0, g_az = 1.0f;

static void frames(Application &app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax;
    AmebaStore::imu_ay = g_ay;
    AmebaStore::imu_az = g_az;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
    }
}

static void tap(Application &app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 1);
}

static void tilt(float ax, float ay) {
    g_ax = ax;
    g_ay = ay;
}

static void level() {
    g_ax = 0;
    g_ay = 0;
}

static uint32_t digest(const uint8_t *bytes) {
    uint32_t h = 2166136261U;
    for (int i = 0; i < 1024; ++i) h = (h ^ bytes[i]) * 16777619U;
    return h;
}

static void image(const char *folder, const char *name, const Arduboy2Compat &engine) {
    assert(digest(engine.sBuffer) != digest(Arduboy2Compat().sBuffer));
    if (!folder) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.pbm", folder, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("P4\n128 64\n", file);
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; x += 8) {
            uint8_t bits = 0;
            for (int b = 0; b < 8; ++b)
                if (!(engine.sBuffer[y / 8 * 128 + x + b] & (1U << (y & 7)))) bits |= 1U << (7 - b);
            fputc(bits, file);
        }
    assert(!fclose(file));
}

static unsigned litPixels(const Canvas &canvas) {
    unsigned lit = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 128; ++x) lit += canvas.pixels[y][x] != 0;
    return lit;
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
    Canvas canvas;
    InputDriver input;
    ChribocchiApp &app = requireApp<ChribocchiApp>();

    AmebaStore::raw_btn_mask = 0;
    level();
    app.onMount(&canvas, &input);

    // Original two-second animated logo, then the hand-composed title menu.
    frames(app, 121);
    assert(ChribocchiSandbox::portState().mode.mode == ChribocchiSandbox::TITLE_MODE);
    assert(ChribocchiSandbox::Logo::portState().counter == 0);
    image(folder, "chribocchi-title", ChribocchiSandbox::portState().engine.arduboy);
    assert(litPixels(canvas) > 0);

    // Record path: the original RAM EEPROM layout/signature and sorted Best 10.
    assert(ChribocchiSandbox::Title::setLastScore(5, 60 * 90) == 0);
    assert(ChribocchiSandbox::Title::portState().hiScore[0] == 5);
    assert(ChribocchiSandbox::portState().engine.arduboy.eeprom.read(832) == 0x4F); // "OBN\x03"
    assert(ChribocchiSandbox::portState().engine.arduboy.eeprom.read(833) == 0x42);
    assert(ChribocchiSandbox::portState().engine.arduboy.eeprom.read(834) == 0x4E);
    assert(ChribocchiSandbox::portState().engine.arduboy.eeprom.read(835) == 0x03);

    // Menu navigation: SOUND toggle, RECORD screen, back, then START GAME.
    tilt(0, 0.5f);
    tap(app, 0); // no PAT, tilt DOWN
    level();
    frames(app, 1);
    assert(ChribocchiSandbox::Title::portState().state == ChribocchiSandbox::Title::STATE_MENU);
    assert(ChribocchiSandbox::Title::portState().menuPos == ChribocchiSandbox::Title::MENU_SOUND);
    // A fresh Arduboy2Audio flag is ON (RAM EEPROM 0xFF), so initTitle()
    // starts with SOUND on and this tap turns it off. The menu must toggle the
    // live compat flag, not a private stub.
    const bool soundBefore =
        ChribocchiSandbox::portState().engine.arduboy.audio.enabled();
    assert(soundBefore);
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Title::portState().sound == !soundBefore);
    assert(ChribocchiSandbox::portState().engine.arduboy.audio.enabled() ==
           ChribocchiSandbox::Title::portState().sound);

    tilt(0, 0.5f);
    frames(app, 1);
    level();
    frames(app, 1);
    assert(ChribocchiSandbox::Title::portState().menuPos == ChribocchiSandbox::Title::MENU_RECORD);
    tap(app, 1U << 5);
    frames(app, 2);
    assert(ChribocchiSandbox::Title::portState().state == ChribocchiSandbox::Title::STATE_RECORD);
    image(folder, "chribocchi-record", ChribocchiSandbox::portState().engine.arduboy);
    assert(litPixels(canvas) > 0);
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Title::portState().state == ChribocchiSandbox::Title::STATE_MENU);

    tilt(0, -0.5f);
    frames(app, 1);
    level();
    frames(app, 1);
    tilt(0, -0.5f);
    frames(app, 1);
    level();
    frames(app, 1);
    assert(ChribocchiSandbox::Title::portState().menuPos == ChribocchiSandbox::Title::MENU_START);
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::portState().mode.mode == ChribocchiSandbox::GAME_MODE);
    image(folder, "chribocchi-ready", ChribocchiSandbox::portState().engine.arduboy);

    // Meow uses DOWN; original animation counter drives it.
    ChribocchiSandbox::Game::portState().meowY = 0;
    tilt(0, 0.5f);
    frames(app, 1);
    level();
    assert(ChribocchiSandbox::Game::portState().meowY > 0);

    // Center the cat and run the real 2-second countdown, first throw and
    // bounce. A center catch raises the score and spawns the second box.
    ChribocchiSandbox::Game::portState().catX = 64;
    frames(app, 260);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_GAME);
    assert(ChribocchiSandbox::Game::portState().score >= 1);
    assert(ChribocchiSandbox::Game::portState().boxCnt >= 2);
    assert(ChribocchiSandbox::Game::portState().timer > 0);
    image(folder, "chribocchi-play", ChribocchiSandbox::portState().engine.arduboy);
    assert(litPixels(canvas) > 0);

    // Pause/resume, including the upstream half-second input guard.
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_PAUSE);
    image(folder, "chribocchi-pause", ChribocchiSandbox::portState().engine.arduboy);
    frames(app, 3); // release the PAT pulse so the next tap is a fresh edge
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_GAME);
    assert(ChribocchiSandbox::Game::portState().ignoreCnt > 0);
    frames(app, 10);
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_GAME);
    frames(app, 25);
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_PAUSE);
    frames(app, 3);
    tap(app, 1U << 5);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_GAME);

    // Boundary clamp on the real cat movement (boxes are in flight, which is
    // why this follows the pause path; a completed game still clamps input).
    tilt(-0.5f, 0);
    frames(app, 80);
    assert(ChribocchiSandbox::Game::portState().catX == 0);
    tilt(0.5f, 0);
    frames(app, 140);
    assert(ChribocchiSandbox::Game::portState().catX == 128);
    level();
    frames(app, 1);

    // A second record write stays sorted and checksum-readable.
    const uint16_t plays = ChribocchiSandbox::Title::portState().playCount;
    ChribocchiSandbox::Title::setLastScore(3, 60 * 30);
    assert(ChribocchiSandbox::Title::portState().hiScore[0] == 5);
    assert(ChribocchiSandbox::Title::portState().hiScore[1] == 3);
    assert(ChribocchiSandbox::Title::portState().playCount == plays + 1);

    // Exit/reentry: transient state resets, RAM record survives, canvas detaches.
    app.onUnmount();
    for (uint8_t value : ChribocchiSandbox::portState().engine.arduboy.sBuffer) assert(value == 0);
    app.onMount(&canvas, &input);
    frames(app, 121);
    assert(ChribocchiSandbox::portState().mode.mode == ChribocchiSandbox::TITLE_MODE);
    assert(ChribocchiSandbox::Game::portState().score == 0);
    assert(ChribocchiSandbox::Game::portState().boxCnt == 0);
    assert(ChribocchiSandbox::Game::portState().catX == 0);
    assert(ChribocchiSandbox::Game::portState().state == ChribocchiSandbox::Game::STATE_START);
    assert(ChribocchiSandbox::Title::portState().recordState == ChribocchiSandbox::Title::RECORD_STORED);
    assert(ChribocchiSandbox::Title::portState().hiScore[0] == 5);
    assert(ChribocchiSandbox::Title::portState().hiScore[1] == 3);

    // Records only use their bounded private RAM window.
    for (int i = 0; i < 832; ++i) assert(ChribocchiSandbox::portState().engine.arduboy.eeprom.read(i) == 0xFF);
    for (int i = 864; i < 1024; ++i) assert(ChribocchiSandbox::portState().engine.arduboy.eeprom.read(i) == 0xFF);

    // Shared PAT-hold exit gesture is still owned by the adapter.
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 17);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 500);

    puts("chribocchi actual game: logo/title/menu/record/start/move/meow/bounce/"
         "pause/ram-record/reset/exit/render passed");
    delete &app;
    return 0;
}
