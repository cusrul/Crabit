// Host harness for the actual awisse 2048 source. Only Canvas, InputDriver,
// AmebaStore telemetry and Arduino timing/random are faked.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
// Real Realtek pgm_read_ptr reads only 16 bits; this port must not use it for
// its native data/function pointers.
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))

#include "app/apps/games/game2048/app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

static void frames(Application &app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 17;
        app.onUpdate(fakeMillis);
    }
}
static void tap(Application &app, uint8_t raw) {
    frames(app, 1, raw);
    frames(app, 4, 0);
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
static int pieces() {
    int n = 0;
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y) n += Game2048Sandbox::portState().play.board[x][y] ? 1 : 0;
    return n;
}
static void clearBoard() {
    memset(Game2048Sandbox::portState().play.board, 0, sizeof(Game2048Sandbox::portState().play.board));
    Game2048Sandbox::portState().play.GameState.score = 0;
    Game2048Sandbox::portState().play.GameState.biggest = 0;
    Game2048Sandbox::portState().play.GameState.running = true;
    Game2048Sandbox::portState().play.GameState.saved = false;
    Game2048Sandbox::portState().play.flash = 0;
    Game2048Sandbox::portState().play.board_full = false;
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
    Game2048App &app = requireApp<Game2048App>();
    app.onMount(&canvas, &input);
    frames(app, 3);
    assert(!Game2048Sandbox::portState().engine.started);
    assert(nonEmpty(canvas)); // real "2048 / PRESS A" start frame
    savePbm(canvas, dir, "game2048-start");

    // The original setup wait is now a frame state; A starts and seeds the game.
    tap(app, 1U << 4);
    assert(Game2048Sandbox::portState().engine.started);
    assert(Game2048Sandbox::portState().play.GameState.running);
    assert(pieces() == 2);
    savePbm(canvas, dir, "game2048-play");

    // Original merge routine: two equal tiles merge once and score 4 (2+2).
    clearBoard();
    Game2048Sandbox::portState().play.board[0][0] = 1;
    Game2048Sandbox::portState().play.board[1][0] = 1;
    assert(Game2048Sandbox::MoveTiles(LEFT_BUTTON));
    assert((Game2048Sandbox::portState().play.board[0][0] & 0x7FFF) == 2);
    assert(Game2048Sandbox::portState().play.board[1][0] == 0);
    assert(Game2048Sandbox::portState().play.GameState.score == 4);

    // Four equal tiles must become two merged tiles, not a chain merge.
    clearBoard();
    for (int x = 0; x < 4; ++x) Game2048Sandbox::portState().play.board[x][0] = 1;
    assert(Game2048Sandbox::MoveTiles(LEFT_BUTTON));
    assert((Game2048Sandbox::portState().play.board[0][0] & 0x7FFF) == 2);
    assert((Game2048Sandbox::portState().play.board[1][0] & 0x7FFF) == 2);
    assert(Game2048Sandbox::portState().play.board[2][0] == 0 && Game2048Sandbox::portState().play.board[3][0] == 0);
    assert(Game2048Sandbox::portState().play.GameState.score == 8);

    // Exercise the same merge through the real adapter/controller path using
    // tilt RIGHT; this proves INPUT_* masks and debounce are wired correctly.
    clearBoard();
    Game2048Sandbox::portState().play.board[0][0] = 1;
    Game2048Sandbox::portState().play.board[1][0] = 1;
    AmebaStore::imu_ax = 0.5f;
    frames(app, 1);
    AmebaStore::imu_ax = 0.0f;
    frames(app, 1);
    assert(Game2048Sandbox::portState().play.GameState.score == 4);
    assert(Game2048Sandbox::portState().play.board[3][0] != 0);

    // A full board with no merge is the original game-over path.
    clearBoard();
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 4; ++y)
            Game2048Sandbox::portState().play.board[x][y] = ((x + y) & 1) ? 2 : 1;
    Game2048Sandbox::portState().play.board_full = true;
    Game2048Sandbox::portState().play.GameState.saved = true;
    Game2048Sandbox::ExecuteMove(LEFT_BUTTON);
    assert(!Game2048Sandbox::portState().play.GameState.running);
    frames(app, 1);
    savePbm(canvas, dir, "game2048-over");

    // New game; then real RAM-EEPROM save/load of the high score.
    Game2048Sandbox::NewGame();
    assert(Game2048Sandbox::portState().play.GameState.running && pieces() == 2);
    Game2048Sandbox::portState().play.GameState.highScore = 1234;
    Game2048Sandbox::portState().play.GameState.score = 1234;
    Game2048Sandbox::portState().play.GameState.saved = false;
    Game2048Sandbox::SaveGame();
    assert(Game2048Sandbox::EEPROM[16] == '2');
    assert(Game2048Sandbox::EEPROM[17] == '0');
    assert(Game2048Sandbox::EEPROM[18] == '4');
    assert(Game2048Sandbox::EEPROM[19] == '8');

    // B is the original save key; exercise it through the real adapter too.
    // StepGame clears the dirty/saved flag while redrawing, so verify the RAM
    // EEPROM signature rather than the transient flag.
    Game2048Sandbox::portState().play.GameState.saved = false;
    for (int i = 16; i < 20; ++i) Game2048Sandbox::EEPROM[i] = 0;
    tap(app, 1U << 5);
    assert(Game2048Sandbox::EEPROM[16] == '2');
    assert(Game2048Sandbox::EEPROM[17] == '0');
    assert(Game2048Sandbox::EEPROM[18] == '4');
    assert(Game2048Sandbox::EEPROM[19] == '8');

    app.onUnmount();
    app.onMount(&canvas, &input);
    frames(app, 2);
    assert(!Game2048Sandbox::portState().engine.started);
    tap(app, 1U << 4);
    assert(Game2048Sandbox::portState().engine.started);
    assert(Game2048Sandbox::portState().play.GameState.highScore == 1234);

    app.onUnmount();
    assert(canvas.displays > 0);
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onMount(&canvas, &input);
    app.onUpdate(fakeMillis + 17);
    assert(app.exitRequested());
    app.onUnmount();
    puts("2048 actual game runtime passed");
    delete &app;
}
