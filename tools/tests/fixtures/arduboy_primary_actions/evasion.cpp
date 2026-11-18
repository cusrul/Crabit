// Real Evasion port + real compat adapter. Proves the resolved single PAT tap
// (bit 4) is B — the real restart on the result screen — and the double tap
// (bit 5) is A, which opens the pause/result menu.
#include "preamble.h"
#include "app/apps/games/evasion/app.h"
#include "harness.h"

namespace S = EvasionSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    EvasionApp &app = requireApp<EvasionApp>();
    app.onMount(&canvas, &input);
    frames(app, 200);
    assert(S::portState().mode.mode == S::MODE_TITLE);

    // Deterministic non-simple run; a single tap (B) starts the game.
    S::portState().common.record.simpleMode = 0;
    S::portState().common.record.speed = 0;
    S::portState().common.record.acceleration = 0;
    S::portState().common.record.density = 1;
    S::portState().common.record.thickness = 1;
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 100 && S::portState().mode.mode != S::MODE_GAME; ++i) frames(app, 1);
    assert(S::portState().mode.mode == S::MODE_GAME);
    for (int i = 0; i < 200 && S::Game::portState().state != S::Game::STATE_PLAYING; ++i)
        frames(app, 1);
    assert(S::Game::portState().state == S::Game::STATE_PLAYING);

    // The result screen is reached through the original handler table; inject
    // only the state so the real B branch (initGame) and A branch (menu) run.
    auto &g = S::Game::portState();
    g.state = S::Game::STATE_OVER;
    const uint16_t restarts = S::portState().common.record.playCount;

    // Single tap = B: the original restart path re-initialises the run.
    tap(app, PAT_SINGLE);
    assert(g.state == S::Game::STATE_START);
    assert(g.state != S::Game::STATE_MENU);
    assert(g.score == 0);
    assert(S::portState().common.record.playCount >= restarts);

    // Double tap = A: the real result menu (RETRY GAME / BACK TO TITLE) opens.
    g.state = S::Game::STATE_OVER;
    tap(app, PAT_DOUBLE);
    assert(g.state == S::Game::STATE_MENU);
    assert(S::getMenuItemCount() == 2);

    app.onUnmount();
    delete &app;
    puts("evasion primary action passed");
    return 0;
}
