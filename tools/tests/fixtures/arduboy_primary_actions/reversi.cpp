// Real Reversi port + real compat adapter. Proves the resolved single PAT tap
// (bit 4) is B — a real stone placement — and the double tap (bit 5) is A,
// which opens the in-game menu.
#include "preamble.h"
#include "app/apps/games/reversi/app.h"
#include "harness.h"

namespace S = ReversiSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    ReversiApp &app = requireApp<ReversiApp>();
    app.onMount(&canvas, &input);
    frames(app, 130);
    assert(S::portState().mode.mode == S::MODE_TITLE);
    assert(S::Title::portState().state == S::Title::STATE_TITLE);

    // AS BLACK, then the level entry: both are real B selections.
    tap(app, PAT_SINGLE);
    assert(S::Title::portState().state == S::Title::STATE_TITLE);
    assert(S::getMenuItemCount() == 4);
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 100 && S::portState().mode.mode != S::MODE_GAME; ++i) frames(app, 1);
    assert(S::portState().mode.mode == S::MODE_GAME);
    frames(app, 150);
    assert(S::portState().game.state == S::STATE_PLAYING);
    assert(S::portState().game.board.numStones == 4);

    auto &g = S::portState().game;
    // Find a real legal empty square flagged by the original rules.
    int px = -1, py = -1;
    for (int y = 0; y < 8 && px < 0; ++y)
        for (int x = 0; x < 8; ++x)
            if ((g.board.flag[y] & (1 << x)) && !(g.board.black[y] & (1 << x)) &&
                    !(g.board.white[y] & (1 << x))) {
                px = x; py = y; break;
            }
    assert(px >= 0);
    g.cursorPos.x = (uint8_t) px;
    g.cursorPos.y = (uint8_t) py;
    const uint8_t placed = g.board.numStones;

    // Single tap = B: the real placeStone path adds stones.
    tap(app, PAT_SINGLE);
    assert(g.state != S::STATE_MENU);
    frames(app, 200);
    assert(g.state == S::STATE_PLAYING);
    assert(g.board.numStones > placed);
    assert(S::portState().common.record.playCount == 1);

    // Single tap on an occupied square is still B and changes nothing; it must
    // not fall through to A's menu.
    int bx = -1, by = -1;
    for (int y = 0; y < 8 && bx < 0; ++y)
        for (int x = 0; x < 8; ++x)
            if (g.board.black[y] & (1 << x)) { bx = x; by = y; break; }
    assert(bx >= 0);
    g.cursorPos.x = (uint8_t) bx;
    g.cursorPos.y = (uint8_t) by;
    const uint8_t steady = g.board.numStones;
    tap(app, PAT_SINGLE);
    assert(g.board.numStones == steady);
    assert(g.state == S::STATE_PLAYING);

    // Double tap = A: the real in-game menu opens.
    tap(app, PAT_DOUBLE);
    assert(g.state == S::STATE_MENU);
    assert(S::getMenuItemCount() >= 2);

    app.onUnmount();
    delete &app;
    puts("reversi primary action passed");
    return 0;
}
