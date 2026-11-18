// Real Stairssweep port + real compat adapter. Proves the resolved single PAT
// tap (bit 4) is B — a real bunch rotation — and the double tap (bit 5) is A,
// which opens the pause menu.
#include "preamble.h"
#include "app/apps/games/stairssweep/app.h"
#include "harness.h"

namespace bs = StairssweepSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    StairssweepApp &app = requireApp<StairssweepApp>();
    app.onMount(&canvas, &input);
    frames(app, 150);
    assert(bs::portState().mode.mode == bs::MODE_TITLE);

    // A single tap must act as B on the title and confirm NOVICE.
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 400 && bs::Game::portState().gameMode != bs::Game::GAME_MODE_CONTROL; ++i)
        frames(app, 1);
    assert(bs::Game::portState().state == bs::Game::STATE_PLAYING);
    assert(bs::Game::portState().gameMode == bs::Game::GAME_MODE_CONTROL);

    auto &g = bs::Game::portState();
    g.gameMode = bs::Game::GAME_MODE_CONTROL;
    g.bunchR = 0;
    g.bunchG = 64;
    g.gameCounter = 30;
    assert(bs::Game::isMoveable(g.bunchX, g.bunchY, 1));

    // Single tap = B alone (no tilt): the original ROTATE mask rotates.
    tap(app, PAT_SINGLE);
    assert(g.bunchR == 1);
    assert(g.state == bs::Game::STATE_PLAYING);
    assert(g.state != bs::Game::STATE_MENU);

    // Keep the bunch settled, then check the other resolved tap.
    g.gameMode = bs::Game::GAME_MODE_CONTROL;
    g.bunchG = 64;
    g.gameCounter = 30;

    // Double tap = A: the real pause menu opens.
    tap(app, PAT_DOUBLE);
    assert(g.state == bs::Game::STATE_MENU);
    assert(bs::getMenuItemCount() == 3);

    app.onUnmount();
    delete &app;
    puts("stairssweep primary action passed");
    return 0;
}
