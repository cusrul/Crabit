#pragma once

// Include-only staging for obono/ArduboyWorks reversi at
// d4b1f041789dcd1d71907654e4025d613b4ab420 (root MIT). Logo and title both own
// a `counter`/`state` plus an `enum STATE_T`, so each gets a nested namespace;
// game/menu stay at sandbox scope for their cross-calls.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef memcpy_P
#define memcpy_P memcpy
#endif

namespace ReversiSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "common.h"
#include "MyArduboy.inc.h"
#include "common.inc.h"
#include "menu.inc.h"

namespace Logo {
#include "logo.inc.h"
}
namespace Title {
#include "title.inc.h"
}
#include "game.inc.h"

void initLogo() { Logo::initLogo(); }
MODE_T updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
MODE_T updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }

#include "reversi.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef record
#undef isRecordDirty
#undef padX
#undef padY
#undef padRepeatCount
#undef isInvalid
#undef recordState
#undef eepAddr
#undef mode
#undef state
#undef board
#undef lastStepBoard
#undef cursorPos
#undef cursorLastPos
#undef lastStepPos
#undef cursorBuf
#undef flipTable
#undef flipStones
#undef animCounter
#undef counter
#undef isCursorMoved
#undef isCancelable
#undef isPreLost
#undef isCpuInterrupted
#undef isLastAPressed
#undef nextCpuInterval
#undef resultSound
#undef resultLabel
#undef dbgPrintEnabled
#undef dbgRecvChar
#undef dbgCaptureMode

struct State {
    CommonState common;
    MenuState menu;
    Logo::LogoState logo;
    Title::TitleState title;
    GameState game;
    ModeState mode;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().common.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_common = &state->common;
    g_menu = &state->menu;
    Logo::g_logo = &state->logo;
    Title::g_title = &state->title;
    g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
    g_menu = nullptr;
    Logo::g_logo = nullptr;
    Title::g_title = nullptr;
    g_game = nullptr;
    g_mode = nullptr;
}

void resetGameState() {
    // Engine/canvas/input reset belongs to ArduboyCompatApp. The RAM EEPROM
    // intentionally survives visits (record is re-read by initTitle).
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.isRecordDirty = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.isInvalid = true;

    // Game state
    s.game.state = STATE_INIT;
    memset(&s.game.board, 0, sizeof(s.game.board));
    memset(&s.game.lastStepBoard, 0, sizeof(s.game.lastStepBoard));
    memset(&s.game.cursorPos, 0, sizeof(s.game.cursorPos));
    memset(&s.game.cursorLastPos, 0, sizeof(s.game.cursorLastPos));
    memset(&s.game.lastStepPos, 0, sizeof(s.game.lastStepPos));
    memset(s.game.cursorBuf, 0, sizeof(s.game.cursorBuf));
    memset(s.game.flipTable, 0, sizeof(s.game.flipTable));
    s.game.flipStones = s.game.animCounter = s.game.counter = 0;
    s.game.isCursorMoved = s.game.isCancelable = s.game.isPreLost = false;
    s.game.isCpuInterrupted = s.game.isLastAPressed = false;
    s.game.nextCpuInterval = 0;
    s.game.resultSound = nullptr;
    s.game.resultLabel = nullptr;

    // Logo / title / menu state
    s.logo.counter = 0;
    s.logo.signalOn = false;
    s.title.state = Title::STATE_INIT;
    s.title.isSettingsChanged = false;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.isFramed = s.menu.isControlSound = false;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.menu.isInvalidMenu = false;
    s.mode.mode = MODE_LOGO;
}

// TEST-STATE-ACCESSORS
// Typed accessors for the live module State. The catalogue App owns
// it; nothing here allocates, and the pointers stay null until
// installPortState() runs.
namespace Title {
inline TitleState &portState() { return *g_title; }
}

} // namespace ReversiSandbox

#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef WIN_LOSE_MAX
#undef WIN_LOSE_VALUE_MAX
#undef SETTING_BIT_THINK_LED
#undef SETTING_BIT_STONES_COUNTER
#undef SETTING_BIT_SHOW_PLACEABLE
#undef SETTING_BIT_SHOW_FIXED
#undef BOARD_W
#undef BOARD_H
#undef INITIAL_STONES
#undef CPU_INTERVAL_FRAMES
#undef CPU_INTERVAL_MILLIS
#undef IMG_STONE_W
#undef IMG_STONE_H
#undef IMG_PLAYER_W
#undef IMG_PLAYER_H
#undef EVAL_INF
#undef EVAL_WIN
#undef EVAL_LOSE
#undef EVAL_NOPLACEABLE
#undef COUNTER_MAX
#undef SIGNAL_PTN
#undef MENU_COUNT_MAX
#undef getDx
#undef getDy
#undef bufferPointer
#undef countBits
#undef dprint
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc
#undef dprintln
