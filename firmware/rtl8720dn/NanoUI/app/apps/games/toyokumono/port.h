#pragma once

// Included once by app.h. The original Toyokumono modules stay private in
// ToyokumonoSandbox; Castle Boy and compatibility macros that precede this
// port in the combined sketch are hidden while the original names compile,
// then restored.
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_GAME_OVER")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef STATE_GAME_OVER
#undef FPS

namespace ToyokumonoSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
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
#undef callHandlerFunc
#undef callDrawerFunc
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

#include "toyokumono.inc.h"

/*  On-demand port state                                               */

#undef ab
#undef record
#undef dots
#undef lastScore
#undef counter
#undef dotIndex
#undef padX
#undef padY
#undef padRepeatCount
#undef isInvalid
#undef isRecordDirty
#undef isTitleAnimation
#undef isInstruction
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

void resetGameState()
{
    // Engine input/pacing/canvas reset belongs to ArduboyCompatApp. The RAM
    // EEPROM intentionally survives visits, so records can be re-read.
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    memset(s.common.dots, 0, sizeof(s.common.dots));
    s.common.lastScore = 0;
    s.common.counter = s.common.dotIndex = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.isInvalid = s.common.isRecordDirty = false;
    s.common.isTitleAnimation = s.common.isInstruction = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    s.menu.isFramed = s.menu.isControlSound = s.menu.isInvalidMenu = false;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.title.state = Title::STATE_INIT;
    s.title.titleFrames = 0;
    s.title.dotsIndex = 0;
    s.game.state = Game::STATE_INIT;
    memset(s.game.clouds, 0, sizeof(s.game.clouds));
    memset(s.game.grounds, 0, sizeof(s.game.grounds));
    memset(s.game.flowers, 0, sizeof(s.game.flowers));
    s.game.led.r = s.game.led.g = s.game.led.b = s.game.led.v = 0;
    s.game.playerX = s.game.playerY = 0;
    s.game.playerVx = s.game.playerVy = 0;
    s.game.windKeepFrames = 0;
    s.game.score = s.game.elements = s.game.gameFrames = s.game.overFrames = 0;
    s.game.currentWind = s.game.targetWind = 0;
    s.game.currentWindGap = s.game.targetWindGap = 0;
    s.game.scoreY = 0;
    s.game.endureFrames = 0;
    s.game.cloudIndex = s.game.flowerIndex = 0;
    s.game.playerImgIndex = 0;
    s.game.instructionPage = 0;
    s.game.isFast = s.game.isHiscore = false;
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

} // namespace ToyokumonoSandbox

// Do not leak this game's macros into later games in the shared sketch TU.
#undef FPS
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef DECIMAL_BITS
#undef DOTS_MAX
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef IMG_CLOUD_W
#undef IMG_CLOUD_H
#undef IMG_PLANT_W
#undef IMG_PLANT_H
#undef IMG_GROUND_W
#undef IMG_GROUND_H
#undef IMG_FLOWER_W
#undef IMG_FLOWER_H
#undef IMG_PLAYER_W
#undef IMG_PLAYER_H
#undef IMG_FAST_W
#undef IMG_FAST_H
#undef IMG_READY_W
#undef IMG_READY_H
#undef IMG_GAMEOVER_W
#undef IMG_GAMEOVER_H
#undef SND_PRIO_START
#undef SND_PRIO_OVER
#undef SND_PRIO_SCATTER
#undef SND_PRIO_SPROUT
#undef SND_PRIO_WITHER
#undef SND_PRIO_DAMAGE
#undef SND_PRIO_WARN
#undef SND_PRIO_CLOUD
#undef CLOUDS_MAX
#undef GROUNDS_MAX
#undef FLOWERS_MAX
#undef PLAYER_Y_MAX
#undef PLAYER_ACCEL
#undef ELEMENT_INITIAL
#undef CLOUD_LIFE_MAX
#undef GROUND_WET_MAX
#undef PLANT_LIFE_MAX
#undef PLANT_POWER_MAX
#undef PLANT_POWER_FINE
#undef PLANT_POWER_SPROUT
#undef PLANT_POWER_RECOVER
#undef PLANT_DECAY
#undef PLANT_HEIGHT_SHIFT
#undef CALM_FRAMES
#undef ENDURE_FRAMES_MAX
#undef INSTRUCTION_FRAMES
#undef IMG_GODDESS1_W
#undef IMG_GODDESS1_H
#undef IMG_GODDESS2_W
#undef IMG_GODDESS2_H
#undef IMG_HAND_W
#undef IMG_HAND_H
#undef IMG_TITLE_W
#undef IMG_TITLE_H
#undef IMG_SOUND_W
#undef IMG_SOUNDM_W
#undef MENU_COUNT_MAX
#undef COUNTER_MAX
#undef SIGNAL_PTN
#undef clamp
#undef circulate
#undef addWithLimit
#undef subWithLimit
#undef callHandlerFunc
#undef callDrawerFunc
#undef callInitFunc
#undef callUpdateFunc
#undef callDrawFunc
#undef loopWithinWidth
#undef state
#undef mode

#pragma pop_macro("FPS")
#pragma pop_macro("STATE_GAME_OVER")
#pragma pop_macro("STATE_TITLE")
