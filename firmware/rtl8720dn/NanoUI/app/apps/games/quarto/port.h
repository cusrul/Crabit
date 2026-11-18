#pragma once

// Include-only staging for obono/ArduboyWorks quarto at
// d4b1f041789dcd1d71907654e4025d613b4ab420 (root MIT). Logo/title own a
// `counter`/`state` and their own STATE_T enum, so both are nested namespaces.
// The optional UP+DOWN overlay-hide gesture is documented as unreachable from
// tilt+tap controls; core play is unchanged.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef memcpy_P
#define memcpy_P memcpy
#endif

namespace QuartoSandbox {
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

#include "quarto.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef record
#undef isScreenInverted
#undef isRecordDirty
#undef padX
#undef padY
#undef padRepeatCount
#undef isInvalid
#undef recordState
#undef eepAddr
#undef mode
#undef state
#undef game
#undef lastGame
#undef isCancelable
#undef isCpuInterrupted
#undef isLastAPressed
#undef isHideOverlay
#undef counter
#undef animCounter
#undef cursorX
#undef cursorY
#undef cursorPiece
#undef cpuX
#undef cpuY
#undef cpuPiece
#undef animSx
#undef animSy
#undef animDx
#undef animDy
#undef restPiecesLeft
#undef hintWinMoves
#undef hintNgPieces
#undef linedPiecesPos
#undef nextCpuInterval
#undef resultSound
#undef resultLabel
#undef dbgPrintEnabled
#undef dbgRecvChar

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
    // MyArduboy cached &eeprom in its constructor; the State was constructed in
    // place, so rebind explicitly.
    state->common.arduboy.audio.storage = &state->common.arduboy.eeprom;
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
    // record intentionally survives visits; title re-reads it and its screen
    // inversion setting on entry.
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.isRecordDirty = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.isInvalid = true;
    s.common.isScreenInverted = false;

    // Game state
    s.game.state = STATE_INIT;
    memset(&s.game.game, 0, sizeof(s.game.game));
    memset(&s.game.lastGame, 0, sizeof(s.game.lastGame));
    s.game.isCancelable = s.game.isCpuInterrupted = false;
    s.game.isLastAPressed = s.game.isHideOverlay = false;
    s.game.counter = s.game.animCounter = 0;
    s.game.cursorX = s.game.cursorY = s.game.cursorPiece = 0;
    s.game.cpuX = s.game.cpuY = s.game.cpuPiece = 0;
    s.game.animSx = s.game.animSy = s.game.animDx = s.game.animDy = s.game.restPiecesLeft = 0;
    s.game.hintWinMoves = s.game.hintNgPieces = s.game.linedPiecesPos = 0;
    s.game.nextCpuInterval = 0;
    s.game.resultSound = nullptr;
    s.game.resultLabel = nullptr;

    // Logo / title / menu state
    s.logo.counter = 0;
    s.logo.signalOn = false;
    s.title.state = Title::STATE_INIT;
    s.title.counter = 0;
    s.title.instPage = s.title.instAttr = 0;
    memset(s.title.instPieces, 0, sizeof(s.title.instPieces));
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

} // namespace QuartoSandbox

#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef BOARD_SIZE
#undef BOARD_EMPTY
#undef PIECE_ATTRS
#undef TURN_MAX
#undef PIECE_MAX
#undef SETTING_BIT_2x2_RULE
#undef SETTING_BIT_THINK_LED
#undef SETTING_BIT_SCREEN_INV
#undef SETTING_BIT_HINT
#undef ANIM_COUNTER_MOVE
#undef ANIM_RESOLUTION
#undef CPU_INTERVAL_FRAMES
#undef CPU_INTERVAL_MILLIS
#undef EVAL_INF
#undef EVAL_WIN
#undef IMG_PIECE_W
#undef IMG_PIECE_H
#undef IMG_CURSOR_W
#undef IMG_CURSOR_H
#undef IMG_PLAYER_W
#undef IMG_PLAYER_H
#undef IMG_HINT_WIN_W
#undef IMG_HINT_WIN_H
#undef IMG_QUARTO_ID_MAX
#undef COUNTER_MAX
#undef SIGNAL_PTN
#undef MENU_COUNT_MAX
#undef circulate
#undef dprint
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef INST_PAGE_MAX
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc
#undef dprintln
