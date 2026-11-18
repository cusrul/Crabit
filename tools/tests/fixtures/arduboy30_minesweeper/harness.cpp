// Host harness for the actual Hexagenic Minesweeper source. Only Canvas,
// InputDriver, AmebaStore telemetry and Arduino timing/random are faked.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
// Reproduce the real Realtek 16-bit pgm_read_ptr; this port must not use it for
// data pointers.
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))

#include "app/apps/games/minesweeper/app.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

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
    MinesweeperApp &app = requireApp<MinesweeperApp>();
    app.onMount(&canvas, &input);
    frames(app, 3);
    assert(MinesweeperSandbox::portState().common.gameState == MinesweeperSandbox::GameState::Playing);
    assert(MinesweeperSandbox::portState().common.firstClick);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "minesweeper-start");

    // First reveal is deferred until the real clickA() release path; it must
    // place exactly ten mines and reveal at least the clicked cell.
    tap(app, 1U << 4);
    assert(!MinesweeperSandbox::portState().common.firstClick);
    int mines = 0, revealed = 0;
    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y) {
            mines += MinesweeperSandbox::portState().common.grid[x][y].isMine ? 1 : 0;
            revealed += MinesweeperSandbox::portState().common.grid[x][y].isRevealed ? 1 : 0;
        }
    assert(mines == 10 && revealed >= 1);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "minesweeper-play");

    // Move the cursor and flag/unflag a real hidden cell with B; A must not
    // reveal a flagged cell.
    move(app, 0.5f, 0.0f);
    assert(MinesweeperSandbox::portState().common.markerX == 1);
    int flagX = -1, flagY = -1;
    for (int x = 0; x < 9 && flagX < 0; ++x)
        for (int y = 0; y < 9; ++y)
            if (!MinesweeperSandbox::portState().common.grid[x][y].isRevealed) { flagX = x; flagY = y; break; }
    assert(flagX >= 0);
    MinesweeperSandbox::portState().common.markerX = flagX;
    MinesweeperSandbox::portState().common.markerY = flagY;
    tap(app, 1U << 5);
    assert(MinesweeperSandbox::portState().common.grid[flagX][flagY].isFlagged);
    tap(app, 1U << 4);
    assert(MinesweeperSandbox::portState().common.grid[flagX][flagY].isFlagged &&
           !MinesweeperSandbox::portState().common.grid[flagX][flagY].isRevealed);
    tap(app, 1U << 5);
    assert(!MinesweeperSandbox::portState().common.grid[flagX][flagY].isFlagged);

    // Lose on a real mine, then let the original two-second result pause pass.
    int mineX = -1, mineY = -1;
    for (int x = 0; x < 9 && mineX < 0; ++x)
        for (int y = 0; y < 9; ++y)
            if (MinesweeperSandbox::portState().common.grid[x][y].isMine) { mineX = x; mineY = y; break; }
    assert(mineX >= 0);
    MinesweeperSandbox::portState().common.markerX = mineX;
    MinesweeperSandbox::portState().common.markerY = mineY;
    tap(app, 1U << 4);
    assert(MinesweeperSandbox::portState().common.gameState == MinesweeperSandbox::GameState::Losing);
    savePbm(canvas, dir, "minesweeper-lost");
    frames(app, 130);
    tap(app, 1U << 4);
    assert(MinesweeperSandbox::portState().common.gameState == MinesweeperSandbox::GameState::Playing);

    // Win condition uses the original countHidden()==10 path: fill every safe
    // cell except one, click the last safe cell, and verify Winning.
    memset(MinesweeperSandbox::portState().common.grid, 0, sizeof(MinesweeperSandbox::portState().common.grid));
    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y) {
            bool mine = (x == 0) || (x == 1 && y == 0);
            MinesweeperSandbox::portState().common.grid[x][y].isMine = mine;
            MinesweeperSandbox::portState().common.grid[x][y].neighbouringMines = 0;
            MinesweeperSandbox::portState().common.grid[x][y].isRevealed = !mine;
        }
    MinesweeperSandbox::portState().common.grid[8][8].isRevealed = false;
    MinesweeperSandbox::portState().common.markerX = 8;
    MinesweeperSandbox::portState().common.markerY = 8;
    MinesweeperSandbox::portState().common.firstClick = false;
    tap(app, 1U << 4);
    assert(MinesweeperSandbox::portState().common.gameState == MinesweeperSandbox::GameState::Winning);
    savePbm(canvas, dir, "minesweeper-won");

    // Re-entry starts clean, and the shared PAT-hold gesture exits.
    app.onUnmount();
    app.onMount(&canvas, &input);
    frames(app, 2);
    assert(MinesweeperSandbox::portState().common.gameState == MinesweeperSandbox::GameState::Playing);
    assert(MinesweeperSandbox::portState().common.firstClick);
    assert(MinesweeperSandbox::portState().common.markerX == 0 && MinesweeperSandbox::portState().common.markerY == 0);
    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y) assert(!MinesweeperSandbox::portState().common.grid[x][y].isMine);
    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 17);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 0);
    puts("minesweeper actual game runtime passed");
    delete &app;
}
