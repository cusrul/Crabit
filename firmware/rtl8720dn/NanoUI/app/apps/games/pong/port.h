#pragma once

// Actual taylorhansen/Pong-Arduboy source, isolated in PongSandbox. The
// original single-player AI, optional two-player controls, scoring, pause and
// win/restart states are retained. Only the AVR display/input/audio boundary is
// replaced; the original 1-second setup delay and USB Serial debug tap are
// converted/removed as noted in README.md.
//
// Macro isolation mirrors the neighbouring ports that share one combined TU.
// The engine and game objects live in an on-demand typed State.
#pragma push_macro("FPS")
#pragma push_macro("WALL_FREQ")
#pragma push_macro("PADDLE_FREQ")
#pragma push_macro("POINT_FREQ")
#pragma push_macro("WALL_DUR")
#pragma push_macro("PADDLE_DUR")
#pragma push_macro("POINT_DUR")
#pragma push_macro("SCORE_MAX")
#pragma push_macro("AI_REACT_X")
#pragma push_macro("AI_RAND")
#pragma push_macro("PADDLE_WIDTH")
#pragma push_macro("PADDLE_HEIGHT")
#pragma push_macro("BALL_SIZE")
#pragma push_macro("LEFT")
#pragma push_macro("RIGHT")
#pragma push_macro("CLEAR_BUFFER")
#pragma push_macro("abs")
#undef FPS
#undef WALL_FREQ
#undef PADDLE_FREQ
#undef POINT_FREQ
#undef WALL_DUR
#undef PADDLE_DUR
#undef POINT_DUR
#undef SCORE_MAX
#undef AI_REACT_X
#undef AI_RAND
#undef PADDLE_WIDTH
#undef PADDLE_HEIGHT
#undef BALL_SIZE
#undef LEFT
#undef RIGHT
#undef CLEAR_BUFFER
#undef abs

namespace PongSandbox {

using Arduboy2 = Arduboy2Compat;
using ArduboyTones = ArduboyTonesCompat;
using ArduboyMath::abs;

#define CLEAR_BUFFER true

#include "vector.hpp"
#include "paddle.hpp"
#include "ball.hpp"
// Declarations see the plain names before the state macros exist.
#include "game.hpp"

struct State {
    Arduboy2 arduboy;
    ArduboyTones sound;
    Ball ball;
    Player1 player1;
    Player2 player2;
    void (*gameTick)();
    bool twoPlayers;
    uint8_t setupWait;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline void installPortState(State *state) { g_portState = state; }
inline void clearPortState() { g_portState = nullptr; }
inline Arduboy2Compat &engine() { return portState().arduboy; }

#define arduboy (portState().arduboy)
#define sound (portState().sound)
#define ball (portState().ball)
#define player1 (portState().player1)
#define player2 (portState().player2)
#define gameTick (portState().gameTick)
#define twoPlayers (portState().twoPlayers)
#define setupWait (portState().setupWait)

#include "vector.inc.h"
#include "paddle.inc.h"
#include "ball.inc.h"
#include "game.inc.h"
#include "pong.inc.h"

#undef setupWait
#undef twoPlayers
#undef gameTick
#undef player2
#undef player1
#undef ball
#undef sound
#undef arduboy

#undef CLEAR_BUFFER

// Called before setup() on every mount. Scores, paddles and ball return to the
// original title defaults; the optional two-player flag is off.
void resetGameState() {
    State &s = portState();
    s.twoPlayers = false;
    s.gameTick = &menuTitle;
    s.setupWait = 0;
    s.ball = Ball();
    s.ball.setPosition(Vector{ WIDTH / 2, HEIGHT / 2 });
    s.ball.setVelocity(Vector{ 1, 1 });
    s.player1.resetScore();
    s.player2.resetScore();
    s.player1.setPosition(Vector{ 9, 24 });
    s.player2.setPosition(Vector{ WIDTH - PADDLE_WIDTH - 9, 24 });
}

} // namespace PongSandbox

#pragma pop_macro("abs")
#pragma pop_macro("CLEAR_BUFFER")
#pragma pop_macro("RIGHT")
#pragma pop_macro("LEFT")
#pragma pop_macro("BALL_SIZE")
#pragma pop_macro("PADDLE_HEIGHT")
#pragma pop_macro("PADDLE_WIDTH")
#pragma pop_macro("AI_RAND")
#pragma pop_macro("AI_REACT_X")
#pragma pop_macro("SCORE_MAX")
#pragma pop_macro("POINT_DUR")
#pragma pop_macro("PADDLE_DUR")
#pragma pop_macro("WALL_DUR")
#pragma pop_macro("POINT_FREQ")
#pragma pop_macro("PADDLE_FREQ")
#pragma pop_macro("WALL_FREQ")
#pragma pop_macro("FPS")
