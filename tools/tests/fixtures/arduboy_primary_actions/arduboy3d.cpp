// Real Arduboy 3D port + real compat adapter. Proves the resolved single PAT
// tap (bit 4) is B — the real Fire path spends mana and sets the reload — and
// the double tap (bit 5) is A, the game's strafe modifier, never B.
#include "preamble.h"
#include "app/apps/games/arduboy3d/app.h"
#include "harness.h"

namespace S = Arduboy3DSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    Arduboy3DApp &app = requireApp<Arduboy3DApp>();
    app.onMount(&canvas, &input);

    // Title menu: a single tap is B, which the menu accepts (A or B upstream).
    frames(app, 5);
    tap(app, PAT_SINGLE);
    frames(app, 220); // original 30-tick "Entering floor" gate, then InGame
    assert(S::Game::player.hp > 0);
    assert(S::Game::player.mana == S::Player::maxMana);

    // Single tap = B: the real Player::Fire path spends mana and starts reload.
    const uint8_t manaBefore = S::Game::player.mana;
    tap(app, PAT_SINGLE);
    assert(S::Game::player.mana == manaBefore - S::Player::manaFireCost);
    assert(S::Game::player.reloadTime > 0);

    // Double tap = A: the real input layer receives the strafe modifier and no
    // fire button. A three-frame window always contains one 30 Hz game tick.
    frames(app, 12); // let the reload finish so B would fire if it leaked in
    tilt(0.5f, 0);
    frames(app, 1, PAT_DOUBLE);
    frames(app, 2);
    const uint8_t held = S::Platform::GetInput();
    level();
    assert((held & INPUT_A) != 0);
    assert((held & INPUT_B) == 0);

    app.onUnmount();
    delete &app;
    puts("arduboy3d primary action passed");
    return 0;
}
