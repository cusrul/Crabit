// Real SameGame port + real compat adapter. Proves the resolved single PAT tap
// (bit 4) is B — a real group removal — and the double tap (bit 5) is A, which
// opens the in-game menu.
#include "preamble.h"
#include "app/apps/games/samegame/app.h"
#include "harness.h"

namespace S = SameGameSandbox;
namespace G = SameGameSandbox::Game;

// getField() layout: three bits per tile, lowest tile at y = 0.
static void setTile(int x, int y, uint8_t object) {
    const uint32_t mask = 7UL << (y * 3);
    G::field.objects[x] = (G::field.objects[x] & ~mask) | (uint32_t(object) << (y * 3));
}

static int tileCount() {
    int n = 0;
    for (int x = 0; x < G::field.width; ++x)
        for (int y = 0; y < G::field.height; ++y)
            if ((G::field.objects[x] >> (y * 3)) & 7) ++n;
    return n;
}

int main() {
    Canvas canvas;
    InputDriver input;
    SameGameApp &app = requireApp<SameGameApp>();
    app.onMount(&canvas, &input);
    frames(app, 150);
    assert(S::mode == S::MODE_TITLE);

    // A single tap must act as B on the title and start the game.
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 100 && S::mode != S::MODE_GAME; ++i) frames(app, 1);
    assert(S::mode == S::MODE_GAME);
    for (int i = 0; i < 100 && G::state != G::STATE_PLAYING; ++i) frames(app, 1);
    assert(G::state == G::STATE_PLAYING);

    // Deterministic board: a three-tile group under the cursor plus an
    // unrelated pair so the field is not judged over after the erase.
    memset(G::field.objects, 0, sizeof(G::field.objects));
    memset(G::linkedFlag, 0, sizeof(G::linkedFlag));
    setTile(0, 0, 1);
    setTile(0, 1, 1);
    setTile(0, 2, 1);
    setTile(10, 0, 2);
    setTile(10, 1, 2);
    G::field.cursorX = 0;
    G::field.cursorY = 0;
    G::checkLinkedObjects();
    assert(G::linkedCount == 3);
    assert(tileCount() == 5);

    const uint16_t beforeScore = G::field.score;
    // Single tap = B: the real erase branch removes the linked group.
    tap(app, PAT_SINGLE);
    assert(G::state == G::STATE_ERASING);
    assert(G::state != G::STATE_MENU);
    assert(G::field.score == beforeScore + 1); // (3 - 2)^2
    for (int i = 0; i < 200 && G::state != G::STATE_PLAYING; ++i) frames(app, 1);
    assert(G::state == G::STATE_PLAYING);
    assert(tileCount() == 2); // exactly the unrelated pair survives

    // Double tap = A: the real in-game menu opens.
    tap(app, PAT_DOUBLE);
    assert(G::state == G::STATE_MENU);

    app.onUnmount();
    delete &app;
    puts("samegame primary action passed");
    return 0;
}
