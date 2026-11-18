#pragma once

// Include-only staging for obono/ArduboyWorks rysk at
// d4b1f041789dcd1d71907654e4025d613b4ab420 (root MIT). RYSK is a single
// sketch; MyArduboyPlaytune is excluded because audio is silent on this build.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace RyskSandbox {
using ArduboyPortMath::abs;
#include "MyArduboy2.h"
#include "MyArduboy2.inc.h"
// The sketch names its own ALIGN_* enum values; compat exposes the same names
// as macros for other ports. Keep this port's meaning local.
#pragma push_macro("ALIGN_LEFT")
#pragma push_macro("ALIGN_CENTER")
#pragma push_macro("ALIGN_RIGHT")
#undef ALIGN_LEFT
#undef ALIGN_CENTER
#undef ALIGN_RIGHT
#include "rysk.inc.h"
#pragma pop_macro("ALIGN_RIGHT")
#pragma pop_macro("ALIGN_CENTER")
#pragma pop_macro("ALIGN_LEFT")

struct State {
    CommonState common;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }

void resetGameState() {
    // Engine/canvas/input reset belongs to ArduboyCompatApp; RYSK has no EEPROM.
    isPlaying = false;
    playerX = playerY = 0;
    playerChr = 4;
    playerAnim = 0;
    memset(cookie, 0, sizeof(cookie));
    score = 0;
    gameTime = 0;
    ledRed = ledGreen = ledBlue = ledLevel = 0;
}

#undef arduboy
#undef isPlaying
#undef playerX
#undef playerY
#undef playerChr
#undef playerAnim
#undef cookie
#undef score
#undef gameTime
#undef ledRed
#undef ledGreen
#undef ledBlue
#undef ledLevel
#undef dbgPrintEnabled
#undef dbgRecvChar

inline Arduboy2Compat &engine() { return portState().common.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_common = &state->common;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
}
} // namespace RyskSandbox

#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef COOKIES
#undef ANIM_MAX
#undef PL_SIZE
#undef CK_SIZE
#undef SCALE
#undef WIDTH_S
#undef HEIGHT_S
#undef PL_SIZE_S
#undef CK_SIZE_S
#undef HIT_DIFF_S
#undef NEW_DIFF_S
#undef GAME_DURATION
#undef IMG_TITLE_W
#undef IMG_TITLE_H
#undef IMG_START1_W
#undef IMG_START1_H
#undef IMG_START2_W
#undef IMG_START2_H
#undef IMG_OVER_W
#undef IMG_OVER_H
#undef IMG_FIGURE_W
#undef IMG_FIGURE_H
#undef IMG_TEN_W
#undef IMG_TEN_H
#undef CLOCK_R
#undef CLOCK_X
#undef CLOCK_Y
#undef LED_LEVEL_START
#undef LED_VALUE
#undef dprint
#undef dprintln
