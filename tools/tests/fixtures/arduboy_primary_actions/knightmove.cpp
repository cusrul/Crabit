// Real Knight Move port + real compat adapter. Proves the resolved single PAT
// tap (bit 4) is B — real fast rolling — and the double tap (bit 5) is A, which
// opens the pause menu.
#include "preamble.h"
#include "app/apps/games/knightmove/app.h"
#include "harness.h"

namespace S = KnightMoveSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    KnightMoveApp &app = requireApp<KnightMoveApp>();
    app.onMount(&canvas, &input);
    frames(app, 130);
    assert(S::portState().mode.mode == S::MODE_TITLE);

    randomSeed(12345);
    // A single tap must act as B on the title and start the game.
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 100 && S::portState().mode.mode != S::MODE_GAME; ++i) frames(app, 1);
    assert(S::portState().mode.mode == S::MODE_GAME);
    for (int i = 0; i < 300 && S::Game::portState().state != S::Game::STATE_MOVING; ++i)
        frames(app, 1);
    assert(S::Game::portState().state == S::Game::STATE_MOVING);

    auto &g = S::Game::portState();
    g.state = S::Game::STATE_MOVING;
    g.knightWait = 0;
    g.knightRad = 0;
    g.isFast = false;
    const uint16_t beforeScore = g.score;

    // Single tap = B alone: the original fast-rolling branch scores.
    tap(app, PAT_SINGLE);
    assert(g.score > beforeScore);
    assert(!g.isMenu);
    assert(g.state != S::Game::STATE_FALLING);

    // Settle the knight, then check the other resolved tap.
    g.state = S::Game::STATE_MOVING;
    g.knightWait = 0;
    g.knightRad = 0;
    g.isFast = false;

    // Double tap = A: the real pause menu opens.
    tap(app, PAT_DOUBLE);
    assert(g.isMenu);

    app.onUnmount();
    delete &app;
    puts("knightmove primary action passed");
    return 0;
}
