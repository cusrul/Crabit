#pragma once

// The AVR core supplies memcpy_P; this target reads the same const tables
// directly. Keep the guard so a core that does provide it still wins.
#ifndef memcpy_P
#define memcpy_P memcpy
#endif

// Included once by app.h. The original Stairs Sweep modules stay private in
// StairsSweepSandbox; Castle Boy and compatibility ALIGN_* macros are hidden
// while the original enum names compile, then restored.
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_GAME_OVER")
#pragma push_macro("FPS")
#pragma push_macro("ALIGN_LEFT")
#pragma push_macro("ALIGN_CENTER")
#pragma push_macro("ALIGN_RIGHT")
#undef STATE_TITLE
#undef STATE_GAME_OVER
#undef FPS
#undef ALIGN_LEFT
#undef ALIGN_CENTER
#undef ALIGN_RIGHT

namespace StairssweepSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "common.h"
#include "MyArduboyV.inc.h"
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
#undef forwardGame
#undef drawGameSub
#undef ROTATE_CANCEL_BUTTONS

void initLogo() { Logo::initLogo(); }
MODE_T updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
MODE_T updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }
void initGame() { Game::initGame(); }
MODE_T updateGame() { return Game::updateGame(); }
void drawGame() { Game::drawGame(); }

#include "stairssweep.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef record
#undef padX
#undef padY
#undef padRepeatCount
#undef startLevel
#undef lastScore
#undef isInvalid
#undef isRecordDirty
#undef recordState
#undef eepAddr
#undef mode

struct State {
    CommonState common;
    MenuState menu;
    Logo::LogoState logo;
    Title::TitleState title;
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
    Logo::g_logo = &state->logo;
    Title::g_title = &state->title;
    Game::g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
    g_menu = nullptr;
    Logo::g_logo = nullptr;
    Title::g_title = nullptr;
    Game::g_game = nullptr;
    g_mode = nullptr;
}

void resetGameState()
{
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.startLevel = 0;
    s.common.lastScore = 0;
    s.common.isInvalid = s.common.isRecordDirty = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    s.menu.isFramed = s.menu.isControlSound = s.menu.isInvalidMenu = false;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.logo.counter = 0;
    s.logo.signalOn = false;
    s.title.state = Title::STATE_INIT;
    s.title.lastChoice = 1;
    s.title.maxChoice = 0;
    s.game.state = Game::STATE_INIT;
    s.game.gameMode = Game::GAME_MODE_WAIT;
    memset(s.game.field, 0, sizeof(s.game.field));
    memset(s.game.currentBunch, 0, sizeof(s.game.currentBunch));
    memset(s.game.nextBunch, 0, sizeof(s.game.nextBunch));
    for (int i = 0; i < 4; ++i) s.game.dyingEnemies[i].y = -1;
    s.game.isFastFall = s.game.isFieldFalled = s.game.isHiscore = false;
    s.game.bunchX = s.game.bunchY = s.game.bunchR = s.game.bunchG = 0;
    s.game.ballX = s.game.ballY = s.game.ballV = s.game.ballG = s.game.ballE = 0;
    s.game.lastX = s.game.lastY = s.game.lastR = s.game.lastG = 0;
    s.game.obtainedScoreY = 0;
    s.game.level = s.game.bunchCount = s.game.fallSpeed = s.game.ballSpeed = 0;
    s.game.ballFreq = s.game.scoreCoef = s.game.nextBall = 0;
    s.game.boxesCount = s.game.enemiesCount = s.game.killedEnemies = s.game.maxHeight = 0;
    s.game.gameCounter = s.game.filledLines = 0;
    s.game.starGuage = s.game.starGuageMax = 0;
    s.game.score = s.game.obtainedScore = 0;
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

} // namespace StairssweepSandbox

#undef FPS
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef HILEVEL3
#undef HISCORE3
#undef HILEVEL4
#undef HISCORE4
#undef LEFT_BUTTON_V
#undef RIGHT_BUTTON_V
#undef UP_BUTTON_V
#undef DOWN_BUTTON_V
#undef ROTATE_DPAD
#undef FLIP_SCREEN
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef IMG_LABEL_W
#undef IMG_LABEL_H
#undef IMG_OBJECT_W
#undef IMG_OBJECT_H
#undef IMG_OBJECT_ID_MAX
#undef IMG_READY_W
#undef IMG_READY_H
#undef IMG_GAMEOVER1_W
#undef IMG_GAMEOVER1_H
#undef IMG_GAMEOVER2_W
#undef IMG_GAMEOVER2_H
#undef FIELD_W
#undef FIELD_H
#undef BUNCH_MAX
#undef ROT_SINGLE
#undef GAP_MAX
#undef FALLSPD_MAX
#undef BALLSPD_MAX
#undef BALLE_STUCK
#undef STARSPD
#undef STARG_INIT
#undef STARG_MAX
#undef STARG_STEP1
#undef STARG_STEP2
#undef FIX_GRACE
#undef ERASE_WAIT
#undef FALL_WAIT
#undef NEXT_WAIT
#undef START_WAIT
#undef OVER_WAIT
#undef DYING_MAX
#undef BONUS_THRES
#undef LEVEL_MAX
#undef LEVEL_NOVIC
#undef LEVEL_FREQ
#undef getScreenX
#undef getScreenY
#undef playCount
#undef state
#undef gameMode
#undef field
#undef currentBunch
#undef nextBunch
#undef dyingEnemies
#undef isFastFall
#undef isFieldFalled
#undef isHiscore
#undef bunchX
#undef bunchY
#undef bunchR
#undef bunchG
#undef ballX
#undef ballY
#undef ballV
#undef ballG
#undef ballE
#undef lastX
#undef lastY
#undef lastR
#undef lastG
#undef obtainedScoreY
#undef level
#undef bunchCount
#undef fallSpeed
#undef ballSpeed
#undef ballFreq
#undef scoreCoef
#undef nextBall
#undef boxesCount
#undef enemiesCount
#undef killedEnemies
#undef maxHeight
#undef gameCounter
#undef filledLines
#undef starGuage
#undef starGuageMax
#undef score
#undef obtainedScore
#undef circulate
#undef roundup
#undef maskbits
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc
#undef MENU_COUNT_MAX
#undef IMG_TITLE_W
#undef IMG_TITLE_H
#undef IMG_TITLE_V
#undef APP_RELEASED_V
#undef COUNTER_MAX
#undef SIGNAL_PTN
#undef IMG_OBN_W
#undef IMG_OBN_H
#undef IMG_OBN_ID_MAX
#undef IMG_SOFT_W
#undef IMG_SOFT_H

#pragma pop_macro("ALIGN_RIGHT")
#pragma pop_macro("ALIGN_CENTER")
#pragma pop_macro("ALIGN_LEFT")
#pragma pop_macro("FPS")
#pragma pop_macro("STATE_GAME_OVER")
#pragma pop_macro("STATE_TITLE")
