#pragma once

// Included once by app.h, after the platform/standard headers. Every former
// translation unit keeps private names inside BananonsenseSandbox so several
// Arduboy games can share one sketch translation unit.

// The upstream AVR core supplies these; the compatibility layer does not.
#ifndef PI
#define PI 3.1415926535897932384626433832795
#define CRABIT_BANANONSENSE_PI_DEFINED 1
#endif

// Actual external-game collisions at this boundary. Castle Boy's global.h
// defines these before the combined launcher includes this port; hide them
// while our private sources compile, then restore the legacy values.
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_GAME_OVER")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef STATE_GAME_OVER
#undef FPS

namespace BananonsenseSandbox {
#include "common.h"
#include "MyArduboy2.inc.h"
#include "common.inc.h"
#include "menu.inc.h"

namespace Logo {
#include "logo.inc.h"
}
namespace Title {
#include "title.inc.h"
}
namespace Game {
#include "game.inc.h"
}
#undef callHandlerFunc
#undef callDrawerFunc

void initLogo() { Logo::initLogo(); }
MODE_T updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
MODE_T updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }
void initGame() { Game::initGame(); }
MODE_T updateGame() { return Game::updateGame(); }
void drawGame() { Game::drawGame(); }

#include "bananonsense.inc.h"

/*  On-demand port state                                               */

#undef ab
#undef record
#undef counter
#undef padX
#undef padY
#undef padRepeatCount
#undef isInvalid
#undef isRecordDirty
#undef recordState
#undef eepAddr
#undef mode

struct State {
    CommonState common;
    MenuState menu;
    Title::TitleState title;
    Game::GameState game;
    ModeState mode;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().common.ab; }

inline void installPortState(State *state) {
    g_portState = state;
    g_common = &state->common;
    g_menu = &state->menu;
    Title::g_title = &state->title;
    Game::g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
    g_menu = nullptr;
    Title::g_title = nullptr;
    Game::g_game = nullptr;
    g_mode = nullptr;
}

void resetGameState() {
    // Engine input/pacing/canvas reset belongs to ArduboyCompatApp. The RAM
    // EEPROM intentionally survives visits, but never power loss or reset.
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.counter = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.isInvalid = s.common.isRecordDirty = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    s.menu.isFramed = s.menu.isControlSound = s.menu.isInvalidMenu = false;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.title.state = Title::STATE_TOP;
    s.game.state = Game::STATE_START;
    memset(&s.game.field, 0, sizeof(s.game.field));
    s.game.timer = s.game.score = 0;
    s.game.targetX = s.game.targetY = 0;
    s.game.cursorX = s.game.cursorY = 0;
    s.game.offsetX = s.game.offsetY = 0;
    memset(s.game.message, 0, sizeof(s.game.message));
    s.mode.mode = MODE_LOGO;
}

// TEST-STATE-ACCESSORS
// Typed accessors for the live module State. The catalogue App owns
// it; nothing here allocates, and the pointers stay null until
// installPortState() runs.
namespace Title {
inline TitleState &portState() { return *g_title; }
}
namespace Game {
inline GameState &portState() { return *g_game; }
}

} // namespace BananonsenseSandbox

// Do not leak this game's macros into later games in the shared sketch TU.
#undef FPS
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef IMG_LETTERS_W
#undef IMG_LETTERS_H
#undef IMG_GAME_OVER_W
#undef IMG_GAME_OVER_H
#undef WAVE_BANANA_LENGTH
#undef WAVE_BANANA_FREQ
#undef SAYINGS_MAX
#undef FIELD_W
#undef FIELD_H
#undef DRAW_FIELD_W
#undef DRAW_FIELD_H
#undef TARGET_SIZE
#undef CURSOR_W
#undef CURSOR_H
#undef DRAW_CURSOR_X
#undef DRAW_CURSOR_Y
#undef BITS_PER_LETTER
#undef TARGET_PATTERN
#undef TIMER_MAX
#undef BITS_PER_SAYING
#undef IMG_SOUND_W
#undef IMG_SOUNDM_W
#undef MENU_COUNT_MAX
#undef COUNTER_MAX
#undef SIGNAL_PTN
#undef IMG_TITLE_BANANA1_W
#undef IMG_TITLE_BANANA1_H
#undef IMG_TITLE_BANANA2_W
#undef IMG_TITLE_BANANA2_H
#undef IMG_TITLE_BANANA3_W
#undef IMG_TITLE_BANANA3_H
#undef IMG_TITLE_LETTERS_W
#undef IMG_TITLE_LETTERS_H
#undef circulate
#undef circulateOne
#undef roundup
#undef maskbits
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc

#ifdef CRABIT_BANANONSENSE_PI_DEFINED
#undef PI
#undef CRABIT_BANANONSENSE_PI_DEFINED
#endif

#pragma pop_macro("FPS")
#pragma pop_macro("STATE_GAME_OVER")
#pragma pop_macro("STATE_TITLE")
