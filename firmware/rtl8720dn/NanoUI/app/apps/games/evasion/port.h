#pragma once

// Include-once body for the Evasion port. Former upstream translation units
// keep private static names inside per-module namespaces; persistent state is
// under EvasionSandbox and reset on every mount.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace EvasionSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "common.h"
#include "common.inc.h"
#include "MyArduboy2.inc.h"
#include "menu.inc.h"

namespace Logo {
#include "logo.inc.h"
}
namespace Title {
#include "title.inc.h"
}
#undef callHandlerFunc
#undef callDrawerFunc
#undef getGameSettings
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

#include "evasion.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef record
#undef counter
#undef padX
#undef padY
#undef padRepeatCount
#undef isInvalid
#undef isRecordDirty
#undef recordState
#undef eepAddr
#undef isInvalidInst
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
inline Arduboy2Compat &engine() { return portState().common.arduboy; }

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

// Called once per mount. RAM EEPROM survives visits; every modal/game field is
// reset so a second mount cannot inherit the previous run.
void resetPortState() {
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.counter = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.isInvalid = s.common.isRecordDirty = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.common.isInvalidInst = false;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    s.menu.isFramed = s.menu.isControlSound = s.menu.isInvalidMenu = false;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.title.state = Title::STATE_INIT;
    s.title.lastSettings = 0;
    s.title.lastSimpleMode = false;
    s.title.isSettingChenged = false;
    s.game.state = Game::STATE_INIT;
    memset(s.game.pillars, 0, sizeof(s.game.pillars));
    memset(s.game.dots, 0, sizeof(s.game.dots));
    s.game.score = s.game.accelInt = 0;
    s.game.pillarsTopIdx = s.game.pillarsSpeed = s.game.pillarsZOdd = 0;
    s.game.dotsBaseX = s.game.dotsBaseZ = 0;
    s.game.playerVx = s.game.nextDistance = 0;
    s.game.shakeX = s.game.shakeY = 0;
    s.game.isHiscore = false;
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

} // namespace EvasionSandbox

// Preprocessor names are not scoped by the sandbox namespace.
#undef ACCEL_INT_BASE
#undef ACCEL_MAX
#undef APP_CODE
#undef APP_RELEASED
#undef APP_TITLE
#undef APP_VERSION
#undef CAM_F
#undef COUNTER_MAX
#undef DENSITY_MAX
#undef DOTS_NUM
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef FPS
#undef IMG_BUTTON_H
#undef IMG_BUTTON_W
#undef IMG_FIGURE_H
#undef IMG_FIGURE_W
#undef IMG_GAMEOVER_H
#undef IMG_GAMEOVER_W
#undef IMG_GUYPART_H
#undef IMG_INSTICON_W
#undef IMG_INST_H
#undef IMG_PLAYER_H
#undef IMG_PLAYER_W
#undef IMG_READY_H
#undef IMG_READY_W
#undef IMG_SOUNDM_W
#undef IMG_SOUNDONOFF_W
#undef IMG_SOUND_W
#undef IMG_SPEAKER_W
#undef IMG_TITLE_H
#undef IMG_TITLE_W
#undef JUMP_FRAMES
#undef MENU_COUNT_MAX
#undef OBJECT_X_MAX
#undef OBJECT_X_MIN
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef PILLARS_NUM
#undef PILLAR_Z_MAX
#undef PLAYER_DX
#undef PLAYER_DY
#undef SCORE_MAX
#undef SIGNAL_PTN
#undef SPEED_MAX
#undef THICKNESS_MAX
#undef callDrawerFunc
#undef callHandlerFunc
#undef circulate
#undef circulateOne
#undef dprint
#undef dprintln
#undef getGameSettings
