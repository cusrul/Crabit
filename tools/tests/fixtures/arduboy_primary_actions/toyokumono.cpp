// Real Toyokumono port + real compat adapter. Proves the resolved single PAT
// tap (bit 4) is B — a real cloud summon — and the double tap (bit 5) is A,
// which opens the pause menu.
#include "preamble.h"
#include "app/apps/games/toyokumono/app.h"
#include "harness.h"

namespace tk = ToyokumonoSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    ToyokumonoApp &app = requireApp<ToyokumonoApp>();
    app.onMount(&canvas, &input);
    frames(app, 250);
    assert(tk::portState().mode.mode == tk::MODE_TITLE);
    assert(tk::getMenuItemPos() == 0);

    // GAME START is the second title item; a single tap (B) activates it.
    tilt(0, 0.6f);
    frames(app, 1);
    level();
    frames(app, 2);
    assert(tk::getMenuItemPos() == 1);
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 100 && tk::portState().mode.mode != tk::MODE_GAME; ++i) frames(app, 1);
    assert(tk::portState().mode.mode == tk::MODE_GAME);
    assert(tk::Game::portState().state == tk::Game::STATE_START);
    for (int i = 0; i < 300 && tk::Game::portState().state != tk::Game::STATE_PLAYING; ++i)
        frames(app, 1);
    assert(tk::Game::portState().state == tk::Game::STATE_PLAYING);

    // Remove the automatic-summon path so only the real B edge can summon.
    auto &g = tk::Game::portState();
    g.elements = 50;
    g.endureFrames = 100;
    g.cloudIndex = 0;
    const unsigned cloudSlots = unsigned(sizeof(g.clouds) / sizeof(g.clouds[0]));
    for (unsigned i = 0; i < cloudSlots; ++i) g.clouds[i].life = 0;
    const uint16_t elementsBefore = g.elements;
    assert(g.clouds[0].life == 0);

    // Single tap = B: the original buttonDown(B) path summons one cloud.
    tap(app, PAT_SINGLE);
    assert(g.state == tk::Game::STATE_PLAYING);
    assert(g.state != tk::Game::STATE_MENU);
    assert(g.elements == elementsBefore - 1);
    assert(g.clouds[0].life > 0);

    // Double tap = A: the original pause menu opens.
    tap(app, PAT_DOUBLE);
    assert(g.state == tk::Game::STATE_MENU);
    assert(tk::getMenuItemCount() == 3);

    app.onUnmount();
    delete &app;
    puts("toyokumono primary action passed");
    return 0;
}
