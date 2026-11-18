// Actual LongCat sources + actual adapter; only hardware/NanoUI are faked.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define memcpy_P memcpy

// Realtek/Ameba core shape: these read only 16 bits. The port must not use
// them for function pointers or 32-bit data pointers.
#define pgm_read_word(addr) (*(const unsigned short *)(addr))
#define pgm_read_ptr(addr)  (*(const unsigned short *)(addr))

// Macros that precede this port in the combined sketch.
#define FPS 60
#define STATE_TITLE 0
#define STATE_PLAY 2

#include "app/apps/games/longcat/app.h"

static_assert(FPS == 60, "FPS restored");
static_assert(STATE_TITLE == 0 && STATE_PLAY == 2, "Castle macros restored");

#ifdef CatKeyLength
#error "CatKeyLength macro leaked past port.h"
#endif

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1.0f;
}
namespace lc = LongCatSandbox;
using lc::GameState;

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

// One original direction command: the cat slides until the original blocked
// move returns to GamePlay.
static void push(Application& app, float ax, float ay, unsigned limit = 200) {
    frames(app, 2, 0, ax, ay, 1.0f);
    for (unsigned i = 0; i < limit && lc::portState().game.getGameState() != GameState::GamePlay; ++i)
        frames(app, 1, 0, ax, ay, 1.0f);
    frames(app, 4);
}

static void tiltOnce(Application& app, float ax, float ay) {
    frames(app, 2, 0, ax, ay, 1.0f);
    frames(app, 6);
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
    LongCatApp &app = requireApp<LongCatApp>();

    static_assert(lc::SaveUtil::MemorySaveLocation + int(sizeof(lc::Save)) <= 1024,
                  "save stays inside the private RAM EEPROM");

    app.onMount(&canvas, &input);
    assert(lc::portState().game.getGameState() == GameState::SplashScreen);
    assert(lc::portState().game.getGameContext().stage == 0);
    assert(lc::portState().game.getGameContext().hero.x == 3 && lc::portState().game.getGameContext().hero.y == 3);
    assert(lc::portState().game.getArduboy().eeprom.read(100 + 2) == 0);
    frames(app, 4);
    image(folder, "longcat-splash", lc::portState().game.getArduboy());

    // One PAT A tap advances exactly one state: the port debounces the
    // adapter's multi-frame tap pulse back to an AVR one-frame edge.
    tap(app, 1U << 4);
    frames(app, 2);
    assert(lc::portState().game.getGameState() == GameState::MainMenu);
    image(folder, "longcat-menu", lc::portState().game.getArduboy());

    // Real tilt navigation: three separate DOWN entries move the cursor to
    // Credits; a fourth would wrap into the Credits action, so also prove one
    // tap selects only once.
    tiltOnce(app, 0, 0.8f);
    tiltOnce(app, 0, 0.8f);
    tiltOnce(app, 0, 0.8f);
    tap(app, 1U << 4);
    frames(app, 2);
    assert(lc::portState().game.getGameState() == GameState::CreditsMenu);
    image(folder, "longcat-credits", lc::portState().game.getArduboy());
    tap(app, 1U << 5);
    frames(app, 2);
    assert(lc::portState().game.getGameState() == GameState::MainMenu);

    // Campaign -> Continue loads the real saved stage 0.
    tap(app, 1U << 4);
    frames(app, 2);
    assert(lc::portState().game.getGameState() == GameState::CampaignMenu);
    image(folder, "longcat-campaign", lc::portState().game.getArduboy());
    tap(app, 1U << 4);
    frames(app, 6);
    assert(lc::portState().game.getGameState() == GameState::GamePlay);
    auto& context = lc::portState().game.getGameContext();
    assert(context.stage == 0);
    assert(context.hero.x == 3 && context.hero.y == 3);
    image(folder, "longcat-play", lc::portState().game.getArduboy());

    // Real level-0 completion: each original direction command slides the cat
    // until the blocked move. The path fills the whole 3x3 pocket.
    push(app, 0, -0.8f);   // Up
    assert(context.hero.x == 3 && context.hero.y == 2);
    assert(context.mapObject.getTile(3, 3) != lc::TileType::Empty);
    image(folder, "longcat-cat-body", lc::portState().game.getArduboy());
    push(app, -0.8f, 0);   // Left
    assert(context.hero.x == 2 && context.hero.y == 2);
    push(app, 0, 0.8f);    // Down
    assert(context.hero.x == 2 && context.hero.y == 4);
    push(app, 0.8f, 0);    // Right
    assert(context.hero.x == 4 && context.hero.y == 4);

    // Final Up: hold until the original win condition is true, capture the
    // real thumbs-up overlay, then let the frame-driven two-second hold run.
    frames(app, 2, 0, 0, -0.8f, 1.0f);
    for (int i = 0; i < 200 && !lc::LevelUtils::checkWin(lc::portState().game); ++i)
        frames(app, 1, 0, 0, -0.8f, 1.0f);
    assert(lc::LevelUtils::checkWin(lc::portState().game));
    assert(lc::portState().game.getGameState() == GameState::Simulate);
    image(folder, "longcat-win", lc::portState().game.getArduboy());
    for (int i = 0; i < 200 && lc::portState().game.getGameState() != GameState::GamePlay; ++i)
        frames(app, 1);
    assert(lc::portState().game.getGameState() == GameState::GamePlay);
    assert(context.stage == 1);
    assert(lc::portState().game.getArduboy().eeprom.read(102) == 1);

    // The real save verifies with the repaired SaveUtil::hash and survives a
    // visit; only the transients reset.
    lc::Save saved = lc::SaveUtil::get();
    assert(saved.lastStage == 1 && saved.randomDifficulty == 0);
    assert(lc::SaveUtil::tryGet(saved));

    // Real movement and blocked boundary on stage 1.
    const int8_t heroX = context.hero.x;
    push(app, 0.8f, 0);
    assert(context.hero.x > heroX);
    assert(context.mapObject.getTile(heroX, context.hero.y) != lc::TileType::Empty);
    image(folder, "longcat-stage1", lc::portState().game.getArduboy());
    const int8_t wallX = context.hero.x;
    frames(app, 10, 0, 0.8f, 0, 1.0f);
    assert(context.hero.x == wallX);

    app.onUnmount();
    app.onMount(&canvas, &input);
    assert(lc::portState().game.getGameState() == GameState::SplashScreen);
    assert(lc::portState().game.getGameContext().stage == 1);
    assert(lc::portState().game.getGameContext().hero.x == 4 && lc::portState().game.getGameContext().hero.y == 2);
    assert(lc::portState().game.getGameContext().mapObject.getTile(3, 3) == lc::TileType::Empty);
    assert(lc::portState().game.getArduboy().eeprom.read(102) == 1);
    assert(lc::SaveUtil::get().lastStage == 1);

    // Injected legal completion state (not a full-input run): one empty cell
    // under the cat, Fixed mode. The original rules then reach EndScreen.
    frames(app, 2);
    tap(app, 1U << 4);
    frames(app, 2);
    tap(app, 1U << 4);
    frames(app, 2);
    tap(app, 1U << 4);
    frames(app, 6);
    assert(lc::portState().game.getGameState() == GameState::GamePlay);
    context = lc::portState().game.getGameContext();
    context.mapObject.reset(lc::TileType::Cat_Body_Vertical);
    context.mapObject.setTile(1, 0, lc::TileType::Empty);
    context.hero.x = 0;
    context.hero.y = 0;
    context.hero.dx = context.hero.dy = 0;
    context.hero.direction = lc::Direction::Center;
    context.hero.lastDirection = lc::Direction::Center;
    lc::portState().game.setGameMode(lc::GameMode::Fixed);
    frames(app, 2, 0, 0.8f, 0, 1.0f);
    for (int i = 0; i < 250 && lc::portState().game.getGameState() != GameState::EndScreen; ++i)
        frames(app, 1, 0, 0.8f, 0, 1.0f);
    assert(lc::portState().game.getGameState() == GameState::EndScreen);
    image(folder, "longcat-end", lc::portState().game.getArduboy());
    tap(app, 1U << 4);
    frames(app, 2);
    assert(lc::portState().game.getGameState() == GameState::MainMenu);
    assert(lc::SaveUtil::get().lastStage == 1);

    // PAT hold (bit 6) is the shared exit gesture.
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 1000);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 200);
    puts("longcat: splash/menu/credits/campaign/move/win/save/reentry/end/exit/render passed");
    delete &app;
}
