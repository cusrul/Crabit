#pragma once

// Actual tuxinator2009/Arduboy_MemoryRhythm source and graphics.h asset,
// isolated in MemoryRhythmSandbox. The original Simon-style learn/play rules,
// xorshift sequence and all sprite/road data are retained; only the AVR
// display/input/audio boundary is replaced by the shared compatibility layer.
#pragma push_macro("FPS")
#pragma push_macro("STATE_MAINMENU")
#pragma push_macro("STATE_LEARN")
#pragma push_macro("STATE_PLAY")
#pragma push_macro("STATE_DONE")
#pragma push_macro("ACTION_IDLE")
#pragma push_macro("ACTION_JUMP")
#pragma push_macro("ACTION_DUCK")
#pragma push_macro("ACTION_SLIDE_LEFT")
#pragma push_macro("ACTION_SLIDE_RIGHT")
#pragma push_macro("ACTION_RAISE_LEFT")
#pragma push_macro("ACTION_RAISE_RIGHT")
#pragma push_macro("ACTION_DONE")
#pragma push_macro("TONE_HIGH_VOLUME")
#pragma push_macro("CLEAR_BUFFER")
#pragma push_macro("BITMAP")
#undef FPS
#undef STATE_MAINMENU
#undef STATE_LEARN
#undef STATE_PLAY
#undef STATE_DONE
#undef ACTION_IDLE
#undef ACTION_JUMP
#undef ACTION_DUCK
#undef ACTION_SLIDE_LEFT
#undef ACTION_SLIDE_RIGHT
#undef ACTION_RAISE_LEFT
#undef ACTION_RAISE_RIGHT
#undef ACTION_DONE
#undef TONE_HIGH_VOLUME
#undef CLEAR_BUFFER
#undef BITMAP

namespace MemoryRhythmSandbox {

// ArduboyTones marks a high-volume note with bit 15; the shared mono adapter
// strips that bit because the STM32 PWM has no volume control.
#define TONE_HIGH_VOLUME 0x8000
#define CLEAR_BUFFER true

// Arduboy2 exposes println(); the shared engine has print()/write(). This tiny
// local subclass keeps the upstream debug-text calls unchanged.
class MemoryRhythmArduboy : public Arduboy2Compat {
public:
    size_t println() { return write('\n'); }
    template <class T>
    size_t println(const T &value) {
        const size_t written = print(value);
        return written + write('\n');
    }
};

// ArduboyTones also has a one-argument continuous tone() used by LEARN; the
// shared adapter already exposes tone(frequency, duration = 0).
class MemoryRhythmTones : public ArduboyTonesCompat {
public:
    explicit MemoryRhythmTones(bool (*enabled)() = nullptr)
        : ArduboyTonesCompat(enabled) {}
};

using Arduboy2 = MemoryRhythmArduboy;
using ArduboyTones = MemoryRhythmTones;
using Sprites = SpritesCompat;

#include "memoryrhythm.inc.h"

/*  On-demand port state                                               */

#undef CLEAR_BUFFER
#undef TONE_HIGH_VOLUME
#undef arduboy
#undef tones
#undef sprites
#undef startSeed
#undef currentSeed
#undef score
#undef octave
#undef action
#undef actionDuration
#undef actionRate
#undef increaseRate
#undef roadOffset
#undef state
#undef numMoves
#undef currentMove

struct State {
    GameStateHolder game;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().game.arduboy; }

// Called before setup() on every mount: returns to the original DONE/start
// screen with score 0, speed 1 and a three-move sequence.
void resetGameState() {
    State &s = portState();
    s.game.score = 0;
    s.game.octave = 0;
    s.game.action = ACTION_IDLE;
    s.game.actionDuration = 0;
    s.game.actionRate = 5;
    s.game.increaseRate = 1;
    s.game.roadOffset = 0;
    s.game.state = STATE_DONE;
    s.game.numMoves = 3;
    s.game.currentMove = 0;
    s.game.startSeed = 0;
    s.game.currentSeed = 0;
}

inline void installPortState(State *state) {
    g_portState = state;
    g_game = &state->game;
    state->game.sprites.setEngine(state->game.arduboy);
}

inline void clearPortState() {
    g_portState = nullptr;
    g_game = nullptr;
}
} // namespace MemoryRhythmSandbox

#pragma pop_macro("BITMAP")
#pragma pop_macro("CLEAR_BUFFER")
#pragma pop_macro("TONE_HIGH_VOLUME")
#pragma pop_macro("ACTION_DONE")
#pragma pop_macro("ACTION_RAISE_RIGHT")
#pragma pop_macro("ACTION_RAISE_LEFT")
#pragma pop_macro("ACTION_SLIDE_RIGHT")
#pragma pop_macro("ACTION_SLIDE_LEFT")
#pragma pop_macro("ACTION_DUCK")
#pragma pop_macro("ACTION_JUMP")
#pragma pop_macro("ACTION_IDLE")
#pragma pop_macro("STATE_DONE")
#pragma pop_macro("STATE_PLAY")
#pragma pop_macro("STATE_LEARN")
#pragma pop_macro("STATE_MAINMENU")
#pragma pop_macro("FPS")
