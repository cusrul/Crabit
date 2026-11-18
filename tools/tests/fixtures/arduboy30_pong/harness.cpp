// Host harness for the actual taylorhansen Pong source. Only Canvas,
// InputDriver, AmebaStore telemetry and Arduino timing/random are fake.
#include "NanoUI/compatibility/geometry.h"

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#define pgm_read_ptr(addr) (*(const unsigned short *)(addr))
#define pgm_read_word(addr) (*(const unsigned short *)(addr))

#define SCORE_MAX 0x1111
#define PADDLE_WIDTH 0x2222
#define PADDLE_HEIGHT 0x3333
#define BALL_SIZE 0x4444
#define LEFT 0x5555
#define RIGHT 0x6666
#define CLEAR_BUFFER 0x7777
#define AI_RAND 0x8888

#include "app/apps/games/pong/app.h"
#if SCORE_MAX != 0x1111 || PADDLE_WIDTH != 0x2222 || PADDLE_HEIGHT != 0x3333 || \
    BALL_SIZE != 0x4444 || LEFT != 0x5555 || RIGHT != 0x6666 || \
    CLEAR_BUFFER != 0x7777 || AI_RAND != 0x8888
#error "Pong did not restore preceding macros"
#endif
#undef SCORE_MAX
#undef PADDLE_WIDTH
#undef PADDLE_HEIGHT
#undef BALL_SIZE
#undef LEFT
#undef RIGHT
#undef CLEAR_BUFFER
#undef AI_RAND

#include <assert.h>
#include <stdio.h>
#include <string.h>

uint32_t fakeMillis = 0;
namespace AmebaStore {
uint8_t raw_btn_mask = 0;
float imu_ax = 0, imu_ay = 0, imu_az = 1;
}

namespace PG = PongSandbox;
using PG::Vector;
static const int kBallSize = 3;      // upstream BALL_SIZE
static const int kPaddleWidth = 4;   // upstream PADDLE_WIDTH

static float g_ax = 0, g_ay = 0;
static void tilt(float ax, float ay) { g_ax = ax; g_ay = ay; }
static void level() { g_ax = g_ay = 0; }

// The game runs at 30 fps; a 34 ms host step advances exactly one game frame.
static void frames(Application &app, unsigned count, uint8_t raw = 0) {
    AmebaStore::raw_btn_mask = raw;
    AmebaStore::imu_ax = g_ax;
    AmebaStore::imu_ay = g_ay;
    AmebaStore::imu_az = 1.0f;
    for (unsigned i = 0; i < count; ++i) {
        fakeMillis += 34;
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

// The split build attributes a game's read-only text by namespace, so these
// named arrays must carry the exact strings the menus print; swapping them back
// to anonymous literals would drop the port's readonly asset.
static void assertScreenText() {
    assert(strcmp(PG::playersLabel, "players:") == 0);
    assert(strcmp(PG::winText1, "Player 1\nwins!\nPress A to\nrestart") == 0);
    assert(strcmp(PG::winText2, "Player 2\nwins!\nPress A to\nrestart") == 0);
}

// The named array must still paint through the same engine call the menus use.
static void assertTextPaints(Canvas &canvas) {
    PG::engine().clear();
    PG::engine().setCursor(0, 0);
    PG::engine().print(PG::playersLabel);
    PG::engine().display();
    bool painted = false;
    for (int y = 0; y < 8 && !painted; ++y)
        for (int x = 0; x < 80; ++x)
            if (canvas.pixels[y][x]) { painted = true; break; }
    assert(painted);
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
static void waitForMain(Application &app) {
    unsigned guard = 0;
    while (PG::portState().gameTick != &PG::gameMain && guard++ < 100) frames(app, 1);
    assert(PG::portState().gameTick == &PG::gameMain);
}
static void forcePoint(Application &app, bool player1Scores) {
    PG::portState().gameTick = &PG::gameMain;
    PG::portState().ball.setPosition(Vector{ player1Scores ? WIDTH - kBallSize - 1 : 0, 32 });
    PG::portState().ball.setVelocity(Vector{ player1Scores ? 1 : -1, 1 });
    frames(app, 1);
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
    PongApp &app = requireApp<PongApp>();
    app.onMount(&canvas, &input);
    frames(app, 3);
    assert(PG::portState().gameTick == &PG::menuTitle);
    assert(!PG::portState().twoPlayers);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "pong-title");
    assertScreenText();

    // Original title toggle: LEFT/RIGHT choose 1 or 2 players; A starts.
    move(app, -0.5f, 0.0f);
    assert(PG::portState().twoPlayers);
    move(app, 0.5f, 0.0f);
    assert(!PG::portState().twoPlayers);
    tap(app, 1U << 4);
    assert(PG::portState().gameTick == &PG::gameWait);
    assert(PG::portState().setupWait > 0);
    waitForMain(app);
    assert(PG::portState().player1.getScore() == 0 && PG::portState().player2.getScore() == 0);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "pong-main");

    // Real ball motion and pause/resume through the original LEFT edge.
    Vector before = PG::portState().ball.getPosition();
    frames(app, 2);
    assert(PG::portState().ball.getPosition().getX() != before.getX() ||
           PG::portState().ball.getPosition().getY() != before.getY());
    move(app, -0.5f, 0.0f);
    assert(PG::portState().gameTick == &PG::gamePause);
    Vector paused = PG::portState().ball.getPosition();
    frames(app, 6);
    assert(PG::portState().ball.getPosition().getX() == paused.getX());
    assert(PG::portState().ball.getPosition().getY() == paused.getY());
    savePbm(canvas, dir, "pong-pause");
    move(app, -0.5f, 0.0f);
    assert(PG::portState().gameTick == &PG::gameMain);
    Vector resumed = PG::portState().ball.getPosition();
    frames(app, 2);
    assert(PG::portState().ball.getPosition().getX() != resumed.getX() ||
           PG::portState().ball.getPosition().getY() != resumed.getY());

    // Real player paddle movement (UP/DOWN) and single-player AI reaction.
    PG::portState().player1.setPosition(Vector{ 9, 24 });
    move(app, 0.0f, -0.5f);
    assert(PG::portState().player1.getPosition().getY() == 23);
    PG::portState().player2.setPosition(Vector{ WIDTH - kPaddleWidth - 9, 24 });
    PG::portState().ball.setPosition(Vector{ 100, 10 });
    PG::portState().player2.move();
    assert(PG::portState().player2.getPosition().getY() == 23);
    PG::portState().ball.setPosition(Vector{ 100, 60 });
    PG::portState().player2.move();
    assert(PG::portState().player2.getPosition().getY() == 24);

    // Real wall bounce.
    PG::portState().ball.setPosition(Vector{ 64, 1 });
    PG::portState().ball.setVelocity(Vector{ 1, -1 });
    PG::portState().ball.move();
    assert(PG::portState().ball.getVelocity().getY() > 0);

    // Real paddle bounce: a left-moving ball at the left paddle turns right.
    PG::portState().player1.setPosition(Vector{ 9, 24 });
    PG::portState().ball.setPosition(Vector{ 12, 28 });
    PG::portState().ball.setVelocity(Vector{ -1, 1 });
    PG::portState().ball.move();
    assert(PG::portState().ball.getVelocity().getX() > 0);

    // Three real player-1 points reach the original Player 1 win screen.
    for (int i = 1; i <= 3; ++i) {
        forcePoint(app, true);
        assert(PG::portState().player1.getScore() == i);
    }
    assert(PG::portState().gameTick == &PG::menuWin1);
    frames(app, 1); // draw the original "Player 1 wins!" screen
    savePbm(canvas, dir, "pong-win1");

    // Original "Press A to restart" runs the frame-driven setup again.
    tap(app, 1U << 4);
    assert(PG::portState().gameTick == &PG::gameWait);
    waitForMain(app);
    assert(PG::portState().player1.getScore() == 0 && PG::portState().player2.getScore() == 0);
    assert(PG::portState().player1.getPosition().getY() == 24);
    assert(PG::portState().player2.getPosition().getY() == 24);

    // Three real player-2 points reach the other original win screen.
    for (int i = 1; i <= 3; ++i) {
        forcePoint(app, false);
        assert(PG::portState().player2.getScore() == i);
    }
    assert(PG::portState().gameTick == &PG::menuWin2);
    frames(app, 1); // draw the original "Player 2 wins!" screen
    savePbm(canvas, dir, "pong-win2");
    tap(app, 1U << 4);
    assert(PG::portState().gameTick == &PG::gameWait);
    waitForMain(app);

    // Re-entry starts at the title with single-player selected.
    app.onUnmount();
    app.onMount(&canvas, &input);
    frames(app, 3);
    assert(PG::portState().gameTick == &PG::menuTitle);
    assert(!PG::portState().twoPlayers);
    assert(nonEmpty(canvas));
    savePbm(canvas, dir, "pong-reentry");

    // Last, because it deliberately paints into the framebuffer.
    assertTextPaints(canvas);

    AmebaStore::raw_btn_mask = 1U << 6;
    app.onUpdate(fakeMillis + 34);
    assert(app.exitRequested());
    app.onUnmount();
    assert(canvas.displays > 0);
    puts("pong actual game runtime passed");
    delete &app;
}
