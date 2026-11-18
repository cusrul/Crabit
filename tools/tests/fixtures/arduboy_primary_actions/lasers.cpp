// Real Lasers port + real compat adapter. Proves the resolved single PAT tap
// (bit 4) is B — warning accept, game start, real polarity toggle — and the
// double tap (bit 5) is A, which opens the pause menu once lasers exist.
#include "preamble.h"
#include "app/apps/games/lasers/app.h"
#include "harness.h"

namespace S = LasersSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    LasersApp &app = requireApp<LasersApp>();
    app.onMount(&canvas, &input);

    // Original logo, then the flicker warning; it only accepts at y == Y_MAX.
    for (int i = 0; i < 700 && !(S::mode == S::MODE_WARN && S::y == 51); ++i) frames(app, 1);
    assert(S::mode == S::MODE_WARN);
    assert(S::y == 51);

    // Single tap = B: accept the warning, then start the game.
    tap(app, PAT_SINGLE);
    assert(S::mode == S::MODE_TITLE);
    tap(app, PAT_SINGLE);
    assert(S::mode == S::MODE_GAME);
    assert(S::state == S::STATE_PLAYING);

    // Wait for a real laser spawn so the A pause path is armed.
    for (int i = 0; i < 500 && S::laserCount == 0; ++i) frames(app, 1);
    assert(S::laserCount > 0);

    // Single tap = B: the real polarity toggle runs and does not pause.
    const bool polarity = S::isPlayerWhite;
    tap(app, PAT_SINGLE);
    assert(S::isPlayerWhite != polarity);
    assert(S::state == S::STATE_PLAYING);

    // Double tap = A: the real pause menu opens.
    tap(app, PAT_DOUBLE);
    assert(S::state == S::STATE_MENU);

    app.onUnmount();
    delete &app;
    puts("lasers primary action passed");
    return 0;
}
