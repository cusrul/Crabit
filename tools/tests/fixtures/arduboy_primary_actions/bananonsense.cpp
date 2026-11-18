// Real Bananonsense port + real compat adapter. Proves the resolved single PAT
// tap (bit 4) is B — the real check/miss action — and the double tap (bit 5)
// is A, which opens the in-game menu.
#include "preamble.h"
#include "app/apps/games/bananonsense/app.h"
#include "harness.h"

namespace bs = BananonsenseSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    BananonsenseApp &app = requireApp<BananonsenseApp>();
    app.onMount(&canvas, &input);

    for (int i = 0; i < 300 && bs::portState().mode.mode != bs::MODE_TITLE; ++i) frames(app, 1);
    assert(bs::portState().mode.mode == bs::MODE_TITLE);

    // Title menu starts on GAME START. A single tap must act as B and enter.
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 200 && bs::portState().mode.mode != bs::MODE_GAME; ++i) frames(app, 1);
    assert(bs::portState().mode.mode == bs::MODE_GAME);
    for (int i = 0; i < 300 && bs::Game::portState().state != bs::Game::STATE_PLAYING; ++i)
        frames(app, 1);
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);

    // Single tap = B: checking the guaranteed BANANA target scores a find.
    auto &g = bs::Game::portState();
    g.cursorX = g.targetX;
    g.cursorY = g.targetY;
    assert(bs::Game::isTarget(g.cursorX, g.cursorY));
    const uint8_t foundScore = g.score;
    const int16_t foundTimer = g.timer;
    tap(app, PAT_SINGLE);
    assert(g.state == bs::Game::STATE_FOUND);
    assert(g.score == foundScore + 1);
    assert(g.state != bs::Game::STATE_MENU);
    assert(g.timer <= foundTimer); // B never costs the -3 s miss penalty here

    // Next wave: a single tap off-target is the real B miss, not A's menu.
    for (int i = 0; i < 300 && g.state != bs::Game::STATE_PLAYING; ++i) frames(app, 1);
    assert(g.state == bs::Game::STATE_PLAYING);
    const int fw = int(sizeof(g.field[0]) / sizeof(g.field[0][0]));
    const int fh = int(sizeof(g.field) / sizeof(g.field[0]));
    int8_t ox = 0, oy = 0;
    while (bs::Game::isTarget(ox, oy)) {
        if (++ox >= fw) { ox = 0; ++oy; }
        assert(oy < fh);
    }
    g.cursorX = ox;
    g.cursorY = oy;
    const int16_t missTimer = g.timer;
    tap(app, PAT_SINGLE);
    assert(g.state == bs::Game::STATE_PLAYING);
    assert(g.state != bs::Game::STATE_MENU);
    assert(missTimer - g.timer >= 150); // ~FPS*3 penalty plus the tapped frames

    // Double tap = A: the real in-game menu opens.
    tap(app, PAT_DOUBLE);
    assert(g.state == bs::Game::STATE_MENU);
    assert(bs::getMenuItemCount() == 2);

    app.onUnmount();
    delete &app;
    puts("bananonsense primary action passed");
    return 0;
}
