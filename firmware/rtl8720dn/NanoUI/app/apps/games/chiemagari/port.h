#pragma once

// Include-only staging for OBONO's Chie Magari Ita (chiemagari). Each former
// Arduino translation unit keeps its file-scope names; the game code lives in
// ChiemagariSandbox so it cannot collide with other Arduboy ports in one TU.
//
// The only source edits are documented in README.md: AVR EEPROM/Print/program
// memory shims, the menu's own namespace, one strict-aliasing-safe checksum
// read, one renamed gallery flag, one defined logo shift, and removal of an
// upstream `static` declaration that only AVR's -fpermissive accepted.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifndef memcpy_P
#define memcpy_P memcpy
#endif

static inline size_t chiemagariStrnlenP(const char *text, size_t limit)
{
    size_t length = 0;
    while (length < limit && text[length] != '\0') length++;
    return length;
}
namespace ChiemagariSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;

#include "common.h"
#include "MyArduboy.inc.h"
#include "common.inc.h"

// menu.cpp and puzzle.cpp both declare `enum STATE_T`; only the always-visible
// puzzle copy stays at sandbox scope so gallery can call its board helpers.
namespace Menu {
#include "menu.inc.h"
}

#include "puzzle.inc.h"
#include "logo.inc.h"
#include "gallery.inc.h"

void initMenu() { Menu::initMenu(); }
MODE_T updateMenu() { return Menu::updateMenu(); }
void drawMenu() { Menu::drawMenu(); }

#include "chiemagari.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef recordState
#undef isDirty
#undef pieceAry
#undef isHelpVisible
#undef clearCount
#undef playFrames
#undef padX
#undef padY
#undef padRepeatCount
#undef helpX
#undef helpY
#undef eepAddr
#undef encPiecesCheckSum
#undef mode
#undef dbgPrintEnabled
#undef dbgRecvChar
#undef dbgCaptureMode

struct State {
    CommonState common;
    Menu::MenuState menu;
    PuzzleState puzzle;
    LogoState logo;
    GalleryState gallery;
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
    Menu::g_menu = &state->menu;
    g_puzzle = &state->puzzle;
    g_logo = &state->logo;
    g_gallery = &state->gallery;
    g_mode = &state->mode;
    // MyArduboy's constructor cached &eeprom on the construction address; the
    // State was constructed in place, so rebind explicitly to be unambiguous.
    state->common.arduboy.audio.storage = &state->common.arduboy.eeprom;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
    Menu::g_menu = nullptr;
    g_puzzle = nullptr;
    g_logo = nullptr;
    g_gallery = nullptr;
    g_mode = nullptr;
}

void resetGameState() {
    // Engine input/pacing/canvas reset belongs to ArduboyCompatApp. The RAM
    // EEPROM intentionally survives visits in this engine, but never power loss.
    State &s = portState();
    s.mode.mode = MODE_LOGO;

    s.common.recordState = RECORD_NOT_READ;
    s.common.isDirty = false;
    s.common.isHelpVisible = true;
    s.common.clearCount = 0;
    s.common.playFrames = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.helpX = HELP_RIGHT_POS;
    s.common.helpY = HEIGHT;
    s.common.eepAddr = 0;
    s.common.encPiecesCheckSum = 0;
    memset(s.common.pieceAry, 0, sizeof(s.common.pieceAry));

    // Puzzle state
    s.puzzle.state = STATE_INIT;
    s.puzzle.toDrawAll = true;
    s.puzzle.cursorX = s.puzzle.cursorY = 0;
    s.puzzle.focusPieceIdx = -1;
    for (int i = 0; i < PIECES; i++) s.puzzle.pieceOrder[i] = i;
    memset(s.puzzle.board, 0, sizeof(s.puzzle.board));
    s.puzzle.creepFrames = s.puzzle.quietFrames = 0;
    s.puzzle.lastPatternIdx = 255;
    s.puzzle.clearEffectCount = 0;
    s.puzzle.isNew = false;

    // Logo / gallery state
    s.logo.counter = 0;
    s.logo.signalOn = false;
    s.gallery.galleryIdx = 0;
    s.gallery.galleryToDrawAll = true;

    // Menu state
    s.menu.state = Menu::STATE_INIT;
    s.menu.toDraw = true;
    s.menu.toDrawFrame = true;
    s.menu.menuCount = 0;
    s.menu.menuPos = 0;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
}

// TEST-STATE-ACCESSORS
// Typed accessors for the live module State. The catalogue App owns
// it; nothing here allocates, and the pointers stay null until
// installPortState() runs.
namespace Menu {
inline MenuState &portState() { return *g_menu; }
}

} // namespace ChiemagariSandbox

// Do not change macro meanings for later games in the shared sketch TU.
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef PIECES
#undef HELP_W
#undef HELP_H
#undef HELP_TOP_LIMIT
#undef HELP_RIGHT_POS
#undef EEPROM_AUDIO_ON_OFF
#undef EEPROM_STORAGE_SPACE_START
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef EEPROM_INIT_CHECKSUM
#undef EEPROM_ADDR_PIECES
#undef EEPROM_SIZE_PIECES
#undef EEPROM_ADDR_CONFIGS
#undef EEPROM_ADDR_PATTERN
#undef EEPROM_SIZE_PATTERN
#undef MAX_PATTERN_COUNT
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef BOARD_W
#undef BOARD_H
#undef EMPTY
#undef COLUMN
#undef FRAMES_5SECS
#undef FRAMES_30SECS
#undef FRAMES_2MINS
#undef SIGNAL_PTN
#undef GALLERY_OFFSET_UNIT_X
#undef GALLERY_OFFSET_PIXEL_X
#undef MENU_COUNT_MAX
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc
#undef dprint
#undef dprintln
