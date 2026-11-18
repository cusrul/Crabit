#pragma once

// Include-once body for the Knight Move port. Former upstream units keep
// private static names inside per-module namespaces; persistent state is under
// KnightMoveSandbox and reset on every mount.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// CastleBoy's game-state macros are visible in the combined launcher TU.
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_STAGE_INTRO")
#pragma push_macro("STATE_PLAY")
#pragma push_macro("STATE_GAME_OVER")
#pragma push_macro("STATE_GAME_FINISHED")
#pragma push_macro("STATE_LEVEL_FINISHED")
#pragma push_macro("STATE_STAGE_FINISHED")
#pragma push_macro("STATE_PLAYER_DIED")
#pragma push_macro("STATE_HELP")
#undef STATE_TITLE
#undef STATE_STAGE_INTRO
#undef STATE_PLAY
#undef STATE_GAME_OVER
#undef STATE_GAME_FINISHED
#undef STATE_LEVEL_FINISHED
#undef STATE_STAGE_FINISHED
#undef STATE_PLAYER_DIED
#undef STATE_HELP

namespace KnightMoveSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "common.h"
#include "common.inc.h"
#include "MyArduboy.inc.h"
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

void initLogo() { Logo::initLogo(); }
MODE_T updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
MODE_T updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }
void initGame() { Game::initGame(); }
MODE_T updateGame() { return Game::updateGame(); }
void drawGame() { Game::drawGame(); }

#include "knightmove.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef record
#undef padX
#undef padY
#undef padRepeatCount
#undef lastScore
#undef isInvalid
#undef isRecordDirty
#undef isBMode
#undef recordState
#undef eepAddr
#undef mode

struct State {
    CommonState common;
    MenuState menu;
    Title::TitleState title;
    Logo::LogoState logo;
    Game::GameState game;
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
    Title::g_title = &state->title;
    Logo::g_logo = &state->logo;
    Game::g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
    g_menu = nullptr;
    Title::g_title = nullptr;
    Logo::g_logo = nullptr;
    Game::g_game = nullptr;
    g_mode = nullptr;
}

// Called once per mount. RAM EEPROM survives visits; every modal/game field is
// reset so a second mount cannot inherit the previous run.
void resetPortState() {
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.lastScore = 0;
    s.common.isInvalid = s.common.isRecordDirty = false;
    s.common.isBMode = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    s.menu.isFramed = s.menu.isControlSound = s.menu.isInvalidMenu = false;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.title.state = Title::STATE_INIT;
    s.title.eyeCounter = 0;
    s.game.state = Game::STATE_START;
    memset(s.game.field, 0, sizeof(s.game.field));
    s.game.score = s.game.point = 0;
    s.game.holes = s.game.knightRad = s.game.heartLife = s.game.heartLifeMax = 0;
    s.game.knightX = s.game.knightY = s.game.knightVx = s.game.knightVy = 0;
    s.game.knightDir = s.game.knightVDir = s.game.knightWait = 0;
    s.game.heartX = s.game.heartY = s.game.heartNorm = s.game.heartNormMax = 0;
    s.game.petitX = s.game.petitY = s.game.petitH = s.game.petitCounter = 0;
    s.game.level = s.game.floors = s.game.continuous = 0;
    s.game.pointLife = s.game.evalLife = s.game.counter = 0;
    s.game.ledRed = s.game.ledGreen = s.game.ledBlue = s.game.ledLevel = 0;
    s.game.isFast = s.game.isMenu = s.game.isHiscore = false;
    s.game.evalLabel = nullptr;
    s.mode.mode = MODE_LOGO;
    s.common.arduboy.setAudioEnabled(true);
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

} // namespace KnightMoveSandbox

// Preprocessor names are not scoped by the sandbox namespace.
#undef APP_CODE
#undef APP_RELEASED
#undef APP_TITLE
#undef APP_VERSION
#undef CONGRATS_1ST
#undef CONGRATS_2ND
#undef COUNTER_MAX
#undef EEPROM_ADDR_BASE
#undef EEPROM_ADDR_MIGRATE
#undef EEPROM_SIGNATURE
#undef EVAL_LIFE_MAX
#undef EYE_BLINK_TIME
#undef FIELD_H
#undef FIELD_W
#undef FLOOR_DEFAULT
#undef FLOOR_H
#undef FLOOR_OPEN
#undef FLOOR_W
#undef FPS
#undef HEART_LIFE_MAX
#undef HOLES_MAX
#undef IMG_CURSOR_H
#undef IMG_CURSOR_W
#undef IMG_EYE_OFFSET
#undef IMG_EYE_W
#undef IMG_FLOOR_H
#undef IMG_FLOOR_W
#undef IMG_HEART_H
#undef IMG_HEART_W
#undef IMG_KNIGHT_H
#undef IMG_KNIGHT_W
#undef IMG_OVER_H
#undef IMG_OVER_W
#undef IMG_PETIT_H
#undef IMG_PETIT_W
#undef IMG_READY_H
#undef IMG_READY_W
#undef IMG_SHADOW_H
#undef IMG_SHADOW_W
#undef IMG_SOUNDM_W
#undef IMG_SOUND_W
#undef IMG_SUBTITLE_H
#undef IMG_SUBTITLE_W
#undef IMG_TITLE_H
#undef IMG_TITLE_W
#undef KNIGHT_DIR_MAX
#undef KNIGHT_JUMP
#undef KNIGHT_RAD_FAST
#undef KNIGHT_RAD_HALF
#undef KNIGHT_RAD_MAX
#undef KNIGHT_WAIT_MAX
#undef LED_IDX_HEART
#undef LED_LEVEL_HEART
#undef LED_LEVEL_POINT
#undef LED_LEVEL_POINT_BLK
#undef LED_VALUE
#undef LEVEL_MAX
#undef MENU_COUNT_MAX
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef PETIT_H_RANGE
#undef POINT_LIFE_MAX
#undef SCORE_MAX
#undef SIGNAL_PTN
#undef callStateHandler
#undef circulate
#undef dprint
#undef dprintln
#pragma pop_macro("STATE_HELP")
#pragma pop_macro("STATE_PLAYER_DIED")
#pragma pop_macro("STATE_STAGE_FINISHED")
#pragma pop_macro("STATE_LEVEL_FINISHED")
#pragma pop_macro("STATE_GAME_FINISHED")
#pragma pop_macro("STATE_GAME_OVER")
#pragma pop_macro("STATE_PLAY")
#pragma pop_macro("STATE_STAGE_INTRO")
#pragma pop_macro("STATE_TITLE")
