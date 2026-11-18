#pragma once

// Included once by app.h. The original JumpBoy modules stay private in
// JumpBoySandbox; macros that precede this port in the combined sketch (or
// collide with the compatibility layer's note constants) are hidden while the
// original names compile, then restored.
#pragma push_macro("FPS")
#pragma push_macro("MODE_TITLE")
#pragma push_macro("MODE_START")
#pragma push_macro("MODE_RUNNING")
#pragma push_macro("MODE_GAMECLEAR")
#pragma push_macro("MODE_GAMEOVER")
#pragma push_macro("NOTE_A1")
#pragma push_macro("NOTE_C2")
#pragma push_macro("NOTE_G2")
#pragma push_macro("NOTE_C3")
#pragma push_macro("NOTE_G3")
#pragma push_macro("NOTE_C4")
#undef FPS
#undef MODE_TITLE
#undef MODE_START
#undef MODE_RUNNING
#undef MODE_GAMECLEAR
#undef MODE_GAMEOVER
#undef NOTE_A1
#undef NOTE_C2
#undef NOTE_G2
#undef NOTE_C3
#undef NOTE_G3
#undef NOTE_C4

namespace JumpBoySandbox {
#include "common.h"

// The engine and the silent tone stub are shared by every module, but were
// defined in the sketch body; define them here so the earlier modules' extern
// declarations resolve to the on-demand state.
struct EngineState {
    JumpBoyArduboy arduboy;
    ArduboyTones sound;
};
static EngineState *g_engine = nullptr;
#define arduboy (g_engine->arduboy)
#define sound (g_engine->sound)

struct GameStateHolder {
    uint8_t pMode = MODE_TITLE;
    uint8_t logicDivider = 0;
    uint8_t startWait = 0;
    uint8_t clearWait = 0;
    uint8_t overWait = 0;
    uint8_t missHold = 0;
    uint8_t pendingLevel = 1;
    bool pWaitFlg = false;
    uint8_t gray = 0;
    float frame = 0;
    float animSpeed;
    uint8_t totalFrames = 4;
    int pX = 20;
    int pY = 0;
    float pSpeed = 0;
    float pASpeed = 2;
    float savepASpeed = 2;
    int prevPX = 20;
    int prevPY = 0;
    int pDir = 270;
    int pYY = 1;
    uint8_t pDirectionY = 1;
    uint8_t pSpeedYInit = 1;
    uint8_t pSpeedY;
    uint8_t boundY = 10;
    uint8_t boundH = 10;
    uint8_t lives = 3;
    uint8_t level = 1;
    int stage = 1;
    int record = 0;
    int coinMax = 1;
    uint8_t titleFlash = 0;
};
static GameStateHolder *g_game = nullptr;
#define pMode (g_game->pMode)
#define logicDivider (g_game->logicDivider)
#define startWait (g_game->startWait)
#define clearWait (g_game->clearWait)
#define overWait (g_game->overWait)
#define missHold (g_game->missHold)
#define pendingLevel (g_game->pendingLevel)
#define pWaitFlg (g_game->pWaitFlg)
#define gray (g_game->gray)
#define frame (g_game->frame)
#define animSpeed (g_game->animSpeed)
#define totalFrames (g_game->totalFrames)
#define pX (g_game->pX)
#define pY (g_game->pY)
#define pSpeed (g_game->pSpeed)
#define pASpeed (g_game->pASpeed)
#define savepASpeed (g_game->savepASpeed)
#define prevPX (g_game->prevPX)
#define prevPY (g_game->prevPY)
#define pDir (g_game->pDir)
#define pYY (g_game->pYY)
#define pDirectionY (g_game->pDirectionY)
#define pSpeedYInit (g_game->pSpeedYInit)
#define pSpeedY (g_game->pSpeedY)
#define boundY (g_game->boundY)
#define boundH (g_game->boundH)
#define lives (g_game->lives)
#define level (g_game->level)
#define stage (g_game->stage)
#define record (g_game->record)
#define coinMax (g_game->coinMax)
#define titleFlash (g_game->titleFlash)
#include "spring.inc.h"
#include "coins.inc.h"
#include "powerups.inc.h"
#include "jumpboy.inc.h"

/*  On-demand port state                                               */

struct State {
    EngineState engine;
    CoinsState coinsState;
    PowerUpsState powerups;
    SpringState spring;
    GameStateHolder game;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }

void resetGameState()
{
    // Engine input/pacing/canvas reset belongs to ArduboyCompatApp. JumpBoy
    // has no persistent save: its stage record is session RAM and resets here.
    pMode = MODE_TITLE;
    logicDivider = 0;
    startWait = clearWait = overWait = missHold = 0;
    pendingLevel = 1;
    pWaitFlg = false;

    gray = 0;
    frame = 0;
    animSpeed = 0.2f;
    totalFrames = 4;

    pX = 20;
    pY = 0;
    pSpeed = 0;
    pASpeed = 2;
    savepASpeed = 2;
    prevPX = 20;
    prevPY = 0;
    pDir = 270;
    pYY = 1;

    pDirectionY = 1;
    pSpeedYInit = 1;
    pSpeedY = pSpeedYInit;

    boundY = 10;
    boundH = 10;

    lives = 3;
    level = 1;
    stage = 1;
    record = 0;
    coinMax = 1;
    titleFlash = 0;

    coin = Coin();
    for (int i = 0; i < CONCURRENT_COIN_MAX; i++) {
        coins[i] = Coins();
    }
    powerup = PowerUps();

    initSpring();
    pScroll = 128;
    pScrollSpeed = 1;
}

// Restore the shared TU: every object-like macro that names an on-demand
// field must be undefined before the next port's headers are parsed.
#undef arduboy
#undef sound
#undef coin
#undef coins
#undef powerup
#undef pScroll
#undef pScrollSpeed
#undef pMode
#undef logicDivider
#undef startWait
#undef clearWait
#undef overWait
#undef missHold
#undef pendingLevel
#undef pWaitFlg
#undef gray
#undef frame
#undef animSpeed
#undef totalFrames
#undef pX
#undef pY
#undef pSpeed
#undef pASpeed
#undef savepASpeed
#undef prevPX
#undef prevPY
#undef pDir
#undef pYY
#undef pDirectionY
#undef pSpeedYInit
#undef pSpeedY
#undef boundY
#undef boundH
#undef lives
#undef level
#undef stage
#undef record
#undef coinMax
#undef titleFlash

inline Arduboy2Compat &engine() { return portState().engine.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_engine = &state->engine;
    g_coins = &state->coinsState;
    g_powerups = &state->powerups;
    g_spring = &state->spring;
    g_game = &state->game;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_engine = nullptr;
    g_coins = nullptr;
    g_powerups = nullptr;
    g_spring = nullptr;
    g_game = nullptr;
}
} // namespace JumpBoySandbox

// Do not leak this game's macros into later games in the shared sketch TU.
#undef MODE_TITLE
#undef MODE_START
#undef MODE_RUNNING
#undef MODE_GAMECLEAR
#undef MODE_GAMEOVER
#undef NOTE_A1
#undef NOTE_C2
#undef NOTE_G2
#undef NOTE_C3
#undef NOTE_G3
#undef NOTE_C4

#pragma pop_macro("NOTE_C4")
#pragma pop_macro("NOTE_G3")
#pragma pop_macro("NOTE_C3")
#pragma pop_macro("NOTE_G2")
#pragma pop_macro("NOTE_C2")
#pragma pop_macro("NOTE_A1")
#pragma pop_macro("MODE_GAMEOVER")
#pragma pop_macro("MODE_GAMECLEAR")
#pragma pop_macro("MODE_RUNNING")
#pragma pop_macro("MODE_START")
#pragma pop_macro("MODE_TITLE")
#pragma pop_macro("FPS")
