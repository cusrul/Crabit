// Real Chie Magari Ita port + real compat adapter. Proves the resolved single
// PAT tap (bit 4) is B — pick and release — and the double tap (bit 5) is A:
// the board menu, and the original rotate modifier while a direction is held.
#include "preamble.h"
#include "app/apps/games/chiemagari/app.h"
#include "harness.h"

namespace S = ChiemagariSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    ChiemagariApp &app = requireApp<ChiemagariApp>();
    app.onMount(&canvas, &input);
    frames(app, 140);
    assert(S::portState().mode.mode == S::MODE_MENU);
    assert(S::Menu::portState().state == S::Menu::STATE_TITLE);

    // Single tap = B: PLAY PUZZLE (first menu item).
    tap(app, PAT_SINGLE);
    assert(S::portState().mode.mode == S::MODE_PUZZLE);
    assert(S::portState().puzzle.state == S::STATE_FREE);

    // Focus a real board piece and pick it through the original B path.
    int bx = -1, by = -1;
    for (int y = 0; y < 9 && bx < 0; ++y)
        for (int x = 0; x < 17; ++x)
            if (S::portState().puzzle.board[y][x].idx >= 0) { bx = x; by = y; break; }
    assert(bx >= 0);
    S::portState().puzzle.cursorX = (int8_t) bx;
    S::portState().puzzle.cursorY = (int8_t) by;
    S::focusPiece(bx, by);
    const int8_t picked = S::portState().puzzle.focusPieceIdx;
    assert(picked >= 0);
    tap(app, PAT_SINGLE);
    assert(S::portState().puzzle.state == S::STATE_PICKED);

    // Double tap = A: the held-A rotate modifier still receives the existing
    // three-frame pulse, which turns the picked piece once while tilt is held.
    // This is the preserved limitation, not a new held gesture.
    const uint8_t oldRot = S::portState().common.pieceAry[picked].rot;
    tilt(0.5f, 0);
    frames(app, 1, PAT_DOUBLE);
    frames(app, 2);
    level();
    assert(S::portState().common.pieceAry[picked].rot != oldRot);

    // Single tap = B: release the piece through the original putPieces path.
    tap(app, PAT_SINGLE);
    assert(S::portState().puzzle.state == S::STATE_FREE);

    // Double tap = A: the real board menu opens (CONTINUE / RESET).
    tap(app, PAT_DOUBLE);
    assert(S::portState().mode.mode == S::MODE_MENU);
    assert(S::Menu::portState().state == S::Menu::STATE_PUZZLE);

    app.onUnmount();
    delete &app;
    puts("chiemagari primary action passed");
    return 0;
}
