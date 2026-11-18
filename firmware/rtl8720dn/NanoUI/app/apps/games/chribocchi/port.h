#pragma once

// Included once by app.h, after the platform/standard headers. Each upstream
// translation unit keeps its private names inside ChribocchiSandbox so the port
// can share a sketch TU with other games without leaking macros or symbols.
namespace ChribocchiSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "common.h"
#include "MyArduboy.inc.h"

// The engine is shared by every modal file but was defined in the mode unit.
struct EngineState {
    MyArduboy arduboy;
};
static EngineState *g_engine = nullptr;
#define arduboy (g_engine->arduboy)

namespace Logo {
#include "logo.inc.h"
}
namespace Title {
#include "title.inc.h"
}

// The upstream .ino-level declaration lives in common.h; implementation is in
// title.cpp. Keep the original call site inside Game working via this wrapper.
uint8_t setLastScore(int score, uint32_t time) { return Title::setLastScore(score, time); }

// game.inc.h has its own text-alignment enum; the shared compatibility header
// also exposes ALIGN_* macros for other ports. Hide ours while including it.
#pragma push_macro("ALIGN_LEFT")
#pragma push_macro("ALIGN_CENTER")
#pragma push_macro("ALIGN_RIGHT")
#undef ALIGN_LEFT
#undef ALIGN_CENTER
#undef ALIGN_RIGHT
namespace Game {
#include "game.inc.h"
}
#pragma pop_macro("ALIGN_RIGHT")
#pragma pop_macro("ALIGN_CENTER")
#pragma pop_macro("ALIGN_LEFT")

void initLogo() { Logo::initLogo(); }
bool updateLogo() { return Logo::updateLogo(); }
void drawLogo() { Logo::drawLogo(); }
void initTitle() { Title::initTitle(); }
bool updateTitle() { return Title::updateTitle(); }
void drawTitle() { Title::drawTitle(); }
void initGame() { Game::initGame(); }
bool updateGame() { return Game::updateGame(); }
void drawGame() { Game::drawGame(); }

#include "chribocchi.inc.h"

/*  On-demand port state                                               */

#undef arduboy
#undef mode

struct State {
    EngineState engine;
    Logo::LogoState logo;
    Title::TitleState title;
    Game::GameState game;
    ModeState mode;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().engine.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_engine = &state->engine;
    Logo::g_logo = &state->logo;
    Title::g_title = &state->title;
    Game::g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_engine = nullptr;
    Logo::g_logo = nullptr;
    Title::g_title = nullptr;
    Game::g_game = nullptr;
    g_mode = nullptr;
}

void resetGameState() {
    // Engine input/pacing/canvas reset belongs to ArduboyCompatApp. Transient
    // game/logo/title UI state is reset here; RAM EEPROM records survive visits
    // in this engine but never power loss, and are re-read by initTitle().
    State &s = portState();
    s.mode.mode = LOGO_MODE;
    s.logo.counter = 0;
    s.logo.signalOn = false;
    s.title.state = Title::STATE_MENU;
    s.title.toDraw = true;
    s.title.sound = false;
    s.title.menuPos = Title::MENU_START;
    s.title.lastScore = 0;
    s.title.recordState = Title::RECORD_NOT_READ;
    s.title.eepAddr = 0;
    s.title.playCount = 0;
    s.title.playFrames = 0;
    for (int i = 0; i < 10; i++) s.title.hiScore[i] = 0;
    s.game.state = Game::STATE_START;
    s.game.toDraw = false;
    s.game.gameFrames = 0;
    s.game.counter = 0;
    s.game.timer = 0;
    s.game.ignoreCnt = 0;
    s.game.score = 0;
    s.game.isHiscore = false;
    s.game.catX = 0;
    s.game.catAnim = 0;
    s.game.meowY = 0;
    s.game.boxCnt = 0;
    s.game.boxIdx = 0;
    memset(s.game.boxAry, 0, sizeof(s.game.boxAry));
    memset(s.game.snowAry, 0, sizeof(s.game.snowAry));
}

// TEST-STATE-ACCESSORS
// Typed accessors for the live module State. The catalogue App owns
// it; nothing here allocates, and the pointers stay null until
// installPortState() runs.
namespace Logo {
inline LogoState &portState() { return *g_logo; }
}
namespace Title {
inline TitleState &portState() { return *g_title; }
}
namespace Game {
inline GameState &portState() { return *g_game; }
}

} // namespace ChribocchiSandbox

// Do not change macro meanings for later games in the shared sketch TU.
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef EEPROM_STORAGE_SPACE_START
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef SIGNAL_PTN
#undef coord
#undef coordInv
#undef secs
#undef mid
#undef sign
#undef BOXES
#undef SNOWS
#undef dprint
#undef dprintln
