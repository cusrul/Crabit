// Real Quarto! port + real compat adapter. Proves the resolved single PAT tap
// (bit 4) is B — a real placement/offer — and the double tap (bit 5) is A,
// which opens the in-game menu.
#include "preamble.h"
#include "app/apps/games/quarto/app.h"
#include "harness.h"

namespace S = QuartoSandbox;

int main() {
    Canvas canvas;
    InputDriver input;
    QuartoApp &app = requireApp<QuartoApp>();
    app.onMount(&canvas, &input);
    frames(app, 130);
    assert(S::portState().mode.mode == S::MODE_TITLE);
    assert(S::getMenuItemCount() == 6);
    assert(S::getMenuItemPos() == 0);

    // Choose the original 2 PLAYERS mode so the turn is deterministic.
    tiltTap(app, 0, 0.5f);
    tiltTap(app, 0, 0.5f);
    assert(S::getMenuItemPos() == 2);
    tap(app, PAT_SINGLE);
    for (int i = 0; i < 100 && S::portState().mode.mode != S::MODE_GAME; ++i) frames(app, 1);
    assert(S::portState().mode.mode == S::MODE_GAME);
    frames(app, 20);
    assert(S::portState().game.state == S::STATE_PLACING);
    assert(S::portState().common.record.gameMode == S::GAME_MODE_2PLAYERS);

    auto &g = S::portState().game;
    g.cursorX = 0;
    g.cursorY = 0;
    assert(g.game.board[0][0] == 0xFF);
    const uint8_t piece = g.game.currentPiece;

    // Single tap = B: the held piece is really placed on the empty cell.
    tap(app, PAT_SINGLE);
    assert(g.state == S::STATE_POST_PLACE);
    assert(g.state != S::STATE_MENU);
    frames(app, 20);
    assert(g.state == S::STATE_CHOOSING);
    assert(g.game.board[0][0] == piece);

    // Single tap = B: offer a real piece to the opponent.
    tap(app, PAT_SINGLE);
    assert(g.state == S::STATE_PRE_PLACE);
    assert(g.state != S::STATE_MENU);
    frames(app, 20);
    assert(g.state == S::STATE_PLACING);

    // Double tap = A: the real in-game menu opens (CONTINUE + QUIT GAME).
    tap(app, PAT_DOUBLE);
    assert(g.state == S::STATE_MENU);
    assert(S::getMenuItemCount() >= 2);

    app.onUnmount();
    delete &app;
    puts("quarto primary action passed");
    return 0;
}
