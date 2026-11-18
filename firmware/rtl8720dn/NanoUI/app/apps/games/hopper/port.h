#pragma once

// Hopper port boundary. The original OBONO translation units are included
// once under HopperSandbox; all mutable runtime state lives in the typed State
// created by CrabitArduboy::createPortState() before AppManager::launch.
//
// The host/Realtek compiler is not AVR: pointer-sized pgm_read_word reads are
// avoided for function pointers and object pointers. Packed bitmap bytes still
// use pgm_read_byte(), which is byte-addressed on every target.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Old-game macros that can precede this port in the combined sketch. Push and
// isolate them so Hopper's enum names and constants cannot be rewritten.
#pragma push_macro("APP_TITLE")
#pragma push_macro("APP_CODE")
#pragma push_macro("APP_VERSION")
#pragma push_macro("APP_RELEASED")
#pragma push_macro("ALIGN_LEFT")
#pragma push_macro("ALIGN_CENTER")
#pragma push_macro("ALIGN_RIGHT")
#pragma push_macro("CENTER_X")
#pragma push_macro("CENTER_Y")
#pragma push_macro("CHIPS")
#pragma push_macro("CHIP_ZRANGE")
#pragma push_macro("EEPROM_ADDR_BASE")
#pragma push_macro("EEPROM_SIGNATURE")
#pragma push_macro("EEPROM_STORAGE_SPACE_START")
#pragma push_macro("FLOORS_LEVEL")
#pragma push_macro("FLOORS_MANAGE")
#pragma push_macro("FLOOR_Z_MAX")
#pragma push_macro("MENU_CREDIT")
#pragma push_macro("MENU_RECORD")
#pragma push_macro("MENU_SOUND")
#pragma push_macro("MENU_START")
#pragma push_macro("PI")
#pragma push_macro("RECORD_INITIAL")
#pragma push_macro("RECORD_NOT_READ")
#pragma push_macro("RECORD_STORED")
#pragma push_macro("SIGNAL_PTN")
#pragma push_macro("STATE_CLEAR")
#pragma push_macro("STATE_CREDIT")
#pragma push_macro("STATE_GAME")
#pragma push_macro("STATE_MENU")
#pragma push_macro("STATE_OVER")
#pragma push_macro("STATE_PAUSE")
#pragma push_macro("STATE_RECORD")
#pragma push_macro("STATE_START")
#pragma push_macro("mid")
#pragma push_macro("nextFloorIdx")
#pragma push_macro("sign")

#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef ALIGN_LEFT
#undef ALIGN_CENTER
#undef ALIGN_RIGHT
#undef CENTER_X
#undef CENTER_Y
#undef CHIPS
#undef CHIP_ZRANGE
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef EEPROM_STORAGE_SPACE_START
#undef FLOORS_LEVEL
#undef FLOORS_MANAGE
#undef FLOOR_Z_MAX
#undef MENU_CREDIT
#undef MENU_RECORD
#undef MENU_SOUND
#undef MENU_START
#undef PI
#undef RECORD_INITIAL
#undef RECORD_NOT_READ
#undef RECORD_STORED
#undef SIGNAL_PTN
#undef STATE_CLEAR
#undef STATE_CREDIT
#undef STATE_GAME
#undef STATE_MENU
#undef STATE_OVER
#undef STATE_PAUSE
#undef STATE_RECORD
#undef STATE_START
#undef mid
#undef nextFloorIdx
#undef sign

namespace HopperSandbox {

using ArduboyPortMath::abs;
using ArduboyPortMath::max;
using ArduboyPortMath::min;

#include "common.h"

// Mode of the original sketch. Kept in the on-demand State like every other
// mutable value.
enum MODE {
    LOGO_MODE = 0,
    TITLE_MODE,
    GAME_MODE
};

struct HopperEngineState {
    MyArduboy2 arduboy;
};

struct HopperLogoState {
    uint8_t counter;
    bool signalOn;
};

struct HopperTitleState {
    uint8_t state;
    bool toDraw;
    bool sound;
    int8_t menuPos;
    int8_t manX, manVy, manType;
    int8_t letterY[6];
    int8_t letterVy[6];
    uint16_t lastScore;
    uint8_t recordState;
    int16_t eepAddr;
    uint16_t hiScore[10];
    uint16_t playCount;
    uint32_t playFrames;
};

struct HopperGameState {
    uint8_t state;
    bool toDraw;
    uint32_t gameFrames;
    int counter;
    int8_t ignoreCnt;
    uint score;
    uint level;
    bool isHiscore;
    int16_t playerX, playerY;
    int8_t playerJump, playerTorque;
    uint8_t playerRotate;
    FLOOR floorAry[FLOORS_MANAGE];
    uint8_t floorIdxFirst, floorIdxLast;
    uint8_t flrTypes[4], flrTypesNum;
    FLOOR *pFloorBlur;
    CHIP chipAry[CHIPS];
    uint16_t chipBaseZ;
    bool isBlink;
};

struct HopperModeState {
    MODE mode;
};

struct State {
    HopperEngineState engine;
    HopperLogoState logo;
    HopperTitleState title;
    HopperGameState game;
    HopperModeState mode;
    // Audited-save ports use a zero-access guard; Hopper keeps a real record
    // at 800..831, so this counter stays unused and is not installed.
    uint16_t unexpectedEepromAccess;
};

// Stable per-port handle: exactly one live State while the App exists; cleared
// before destruction and never allocated during static initialization.
static void *g_portState = nullptr;
static HopperLogoState *g_logo = nullptr;
static HopperTitleState *g_title = nullptr;
static HopperGameState *g_game = nullptr;
static HopperModeState *g_mode = nullptr;

inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().engine.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_logo = &state->logo;
    g_title = &state->title;
    g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_logo = nullptr;
    g_title = nullptr;
    g_game = nullptr;
    g_mode = nullptr;
}

// Unqualified upstream references resolve to the live State.
#define arduboy (portState().engine.arduboy)

/*  Logo body  */
#define counter (g_logo->counter)
#define signalOn (g_logo->signalOn)
namespace Logo {
#include "logo.inc.h"
}
#undef counter
#undef signalOn

/*  Title body  */
#define state (g_title->state)
#define toDraw (g_title->toDraw)
#define sound (g_title->sound)
#define menuPos (g_title->menuPos)
#define manX (g_title->manX)
#define manVy (g_title->manVy)
#define manType (g_title->manType)
#define letterY (g_title->letterY)
#define letterVy (g_title->letterVy)
#define lastScore (g_title->lastScore)
#define recordState (g_title->recordState)
#define eepAddr (g_title->eepAddr)
#define hiScore (g_title->hiScore)
#define playCount (g_title->playCount)
#define playFrames (g_title->playFrames)
namespace Title {
#include "title.inc.h"
}
#undef state
#undef toDraw
#undef sound
#undef menuPos
#undef manX
#undef manVy
#undef manType
#undef letterY
#undef letterVy
#undef lastScore
#undef recordState
#undef eepAddr
#undef hiScore
#undef playCount
#undef playFrames

/*  Game body  */
#define state (g_game->state)
#define toDraw (g_game->toDraw)
#define gameFrames (g_game->gameFrames)
#define counter (g_game->counter)
#define ignoreCnt (g_game->ignoreCnt)
#define score (g_game->score)
#define level (g_game->level)
#define isHiscore (g_game->isHiscore)
#define playerX (g_game->playerX)
#define playerY (g_game->playerY)
#define playerJump (g_game->playerJump)
#define playerTorque (g_game->playerTorque)
#define playerRotate (g_game->playerRotate)
#define floorAry (g_game->floorAry)
#define floorIdxFirst (g_game->floorIdxFirst)
#define floorIdxLast (g_game->floorIdxLast)
#define flrTypes (g_game->flrTypes)
#define flrTypesNum (g_game->flrTypesNum)
#define pFloorBlur (g_game->pFloorBlur)
#define chipAry (g_game->chipAry)
#define chipBaseZ (g_game->chipBaseZ)
#define isBlink (g_game->isBlink)
namespace Game {
#include "game.inc.h"
}
#undef state
#undef toDraw
#undef gameFrames
#undef counter
#undef ignoreCnt
#undef score
#undef level
#undef isHiscore
#undef playerX
#undef playerY
#undef playerJump
#undef playerTorque
#undef playerRotate
#undef floorAry
#undef floorIdxFirst
#undef floorIdxLast
#undef flrTypes
#undef flrTypesNum
#undef pFloorBlur
#undef chipAry
#undef chipBaseZ
#undef isBlink

/*  Module entry points used by the upstream sketch table.  */
void initLogo() { Logo::initLogo(); }
bool updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
bool updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }
void initGame() { Game::initGame(); }
bool updateGame() { return Game::updateGame(); }
void drawGame() { Game::drawGame(); }

// Upstream game.cpp calls the title-owned record writer. The declaration in
// common.h is in this namespace, so this wrapper is the visible definition.
uint8_t setLastScore(int score, uint32_t time) {
    return Title::setLastScore(score, time);
}

#define mode (g_mode->mode)
#include "hopper.inc.h"
#undef mode

// Called once per mount, before setup(). The RAM EEPROM record deliberately
// survives visits (the runtime commits it to the retained save slot when the
// Application is destroyed); every other field returns to a fresh sketch.
void resetPortState() {
    State &s = portState();
    s.logo.counter = 0;
    s.logo.signalOn = false;

    s.title.state = Title::STATE_MENU;
    s.title.toDraw = true;
    s.title.sound = true;
    s.title.menuPos = Title::MENU_START;
    s.title.manX = s.title.manVy = s.title.manType = 0;
    memset(s.title.letterY, 0, sizeof(s.title.letterY));
    memset(s.title.letterVy, 0, sizeof(s.title.letterVy));
    s.title.lastScore = 0;
    s.title.recordState = Title::RECORD_NOT_READ;
    s.title.eepAddr = 0;
    memset(s.title.hiScore, 0, sizeof(s.title.hiScore));
    s.title.playCount = 0;
    s.title.playFrames = 0;

    s.game.state = Game::STATE_START;
    s.game.toDraw = false;
    s.game.gameFrames = 0;
    s.game.counter = 0;
    s.game.ignoreCnt = 0;
    s.game.score = 0;
    s.game.level = 0;
    s.game.isHiscore = false;
    s.game.playerX = s.game.playerY = 0;
    s.game.playerJump = s.game.playerTorque = 0;
    s.game.playerRotate = 196;
    memset(s.game.floorAry, 0, sizeof(s.game.floorAry));
    s.game.floorIdxFirst = s.game.floorIdxLast = 0;
    memset(s.game.flrTypes, 0, sizeof(s.game.flrTypes));
    s.game.flrTypesNum = 0;
    s.game.pFloorBlur = nullptr;
    memset(s.game.chipAry, 0, sizeof(s.game.chipAry));
    s.game.chipBaseZ = 0;
    s.game.isBlink = false;

    s.mode.mode = LOGO_MODE;
}

} // namespace HopperSandbox

// Upstream private macros are not scoped by the namespace.
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef CENTER_X
#undef CENTER_Y
#undef CHIPS
#undef CHIP_ZRANGE
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef EEPROM_STORAGE_SPACE_START
#undef FLOORS_LEVEL
#undef FLOORS_MANAGE
#undef FLOOR_Z_MAX
#undef PI
#undef arduboy
#undef mid
#undef nextFloorIdx
#undef sign

#pragma pop_macro("sign")
#pragma pop_macro("nextFloorIdx")
#pragma pop_macro("mid")
#pragma pop_macro("STATE_START")
#pragma pop_macro("STATE_RECORD")
#pragma pop_macro("STATE_PAUSE")
#pragma pop_macro("STATE_OVER")
#pragma pop_macro("STATE_MENU")
#pragma pop_macro("STATE_GAME")
#pragma pop_macro("STATE_CREDIT")
#pragma pop_macro("STATE_CLEAR")
#pragma pop_macro("SIGNAL_PTN")
#pragma pop_macro("RECORD_STORED")
#pragma pop_macro("RECORD_NOT_READ")
#pragma pop_macro("RECORD_INITIAL")
#pragma pop_macro("PI")
#pragma pop_macro("MENU_START")
#pragma pop_macro("MENU_SOUND")
#pragma pop_macro("MENU_RECORD")
#pragma pop_macro("MENU_CREDIT")
#pragma pop_macro("FLOOR_Z_MAX")
#pragma pop_macro("FLOORS_MANAGE")
#pragma pop_macro("FLOORS_LEVEL")
#pragma pop_macro("EEPROM_STORAGE_SPACE_START")
#pragma pop_macro("EEPROM_SIGNATURE")
#pragma pop_macro("EEPROM_ADDR_BASE")
#pragma pop_macro("CHIP_ZRANGE")
#pragma pop_macro("CHIPS")
#pragma pop_macro("CENTER_Y")
#pragma pop_macro("CENTER_X")
#pragma pop_macro("ALIGN_RIGHT")
#pragma pop_macro("ALIGN_CENTER")
#pragma pop_macro("ALIGN_LEFT")
#pragma pop_macro("APP_RELEASED")
#pragma pop_macro("APP_VERSION")
#pragma pop_macro("APP_CODE")
#pragma pop_macro("APP_TITLE")
