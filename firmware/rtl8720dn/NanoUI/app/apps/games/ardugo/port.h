#pragma once

// Include-only staging for ModusCreateOrg/ardugo at
// 2343b903c36234c302efaaa5c3aca80010d388ee (MIT). Each former Arduino
// translation unit is kept as an .inc.h body inside ArduGoSandbox so the game
// can share a translation unit with the other Arduboy ports.
//
// Documented adaptations: old Arduboy/ArduboyTunes/start()/println() are mapped
// to Arduboy2Compat and silent stubs; the debug serial Shell/MemoryFree files
// are excluded; const-correct bitmap helper; frame pacing/button polling added
// to the original loop; app-reentry reset for the board's blink/button state.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ArduGoSandbox {

#include "ArduGo.h"
#include "Board.h"
#include "sidebar.h"

#include "ArduGo.inc.h"
#include "Board.inc.h"
#include "move.inc.h"
#include "validate.inc.h"
#include "simple-play.inc.h"
#include "simple-rules.inc.h"
#include "sidebar.inc.h"
#include "game.inc.h"

/*  On-demand port state                                               */

struct State {
    CommonState common;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
void resetGameState() {
    // The engine/canvas/input reset belongs to ArduboyCompatApp. The game has
    // no EEPROM, so reentry starts a clean board and counters.
    board.resetRuntime();
    black_live = 0;
    white_live = 0;
    black_capt = 0;
    white_capt = 0;
    previousButtonState = 0;
    currentButtonState = 0;
    injectButtonState = 0;
    cur_sidebar = TITLE_SIDEBAR;
}

#undef arduboy
#undef tunes
#undef board

inline Arduboy2Compat &engine() { return portState().common.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_common = &state->common;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
}
} // namespace ArduGoSandbox

// Do not change macro meanings for later games in the shared sketch TU.
#undef SIMPLE_PLAY
#undef SIMPLE_RULES
