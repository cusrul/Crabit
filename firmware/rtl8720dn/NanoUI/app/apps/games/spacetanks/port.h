#pragma once

// Included once by app.h. All original header bodies are isolated in this
// namespace; the port boundary also hides/restores the Castle Boy macros that
// precede it in the combined sketch TU.
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_GAME_OVER")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef STATE_GAME_OVER
#undef FPS

namespace SpaceTanksSandbox {
// Powered-session auto-fire preference, ON by default. It survives leaving and
// re-entering the port (the port has no EEPROM) and is lost on power-off, so
// re-entry cannot silently switch fire back on after the player chose OFF. The
// banner is the only non-battle screen and owns the toggle; one flag, not game
// state: fire still needs the real GAME_STATE_PLAYING path, so this alone never
// fires from the banner, the intro or the death message.
inline bool &autoFirePreference() {
    static bool enabled = true;
    return enabled;
}
#include "globals.h"
#include "state.h"
#include "spacetanks.inc.h"

static const uint8_t initialMovingStar[11][2] = {
    {84, 9}, {67, 14}, {109, 19}, {27, 24}, {9, 29}, {96, 34}, {31, 39},
    {45, 44}, {82, 49}, {19, 54}, {57, 59}
};
static const uint8_t initialFastStar[11][2] = {
    {16, 10}, {125, 15}, {62, 20}, {79, 25}, {115, 30}, {104, 35}, {16, 40},
    {117, 45}, {65, 50}, {98, 55}, {23, 60}
};
static const uint8_t initialStillStar[11][2] = {
    {84, 7}, {67, 12}, {109, 17}, {27, 22}, {9, 27}, {96, 32}, {31, 37},
    {45, 42}, {82, 47}, {19, 52}, {57, 57}
};

struct State {
    EngineState engine;
    GameStateHolder game;
    IntroState intro;
    FlashState flash;
    DeathState death;
    StateHolder state;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }


void resetGameState()
{
    gameState = GAME_STATE_BANNER;
    introMessage = 0;
    introFinished = false;
    introFrameCount = 0;
    deathFrameCount = 0;
    flashingMessageText = NULL;
    flashingMessageFrame = 0;
    flashingMessageX = flashingMessageY = 0;
    currentHitSpaceBat = NULL;
    score = 0;
    currentShotCooldown = 0;
    megadraculaLives = false;
    nextDraculaAtScore = 0;
    nextMegadraculaAtScore = 0;
    memset(&player, 0, sizeof(player));
    memset(spaceBats, 0, sizeof(spaceBats));
    for (int i = 0; i < 11; ++i) {
        memcpy(movingStar[i], initialMovingStar[i], sizeof(movingStar[i]));
        memcpy(fastMovingStar[i], initialFastStar[i], sizeof(fastMovingStar[i]));
        memcpy(stillStar[i], initialStillStar[i], sizeof(stillStar[i]));
    }
}

#undef arduboy
#undef score
#undef player
#undef currentShotCooldown
#undef spaceBats
#undef currentHitSpaceBat
#undef megadraculaLives
#undef nextDraculaAtScore
#undef nextMegadraculaAtScore
#undef movingStar
#undef fastMovingStar
#undef stillStar
#undef introMessage
#undef introFinished
#undef introFrameCount
#undef flashingMessageText
#undef flashingMessageFrame
#undef flashingMessageX
#undef flashingMessageY
#undef deathFrameCount
#undef deathMessage
#undef gameState

inline Arduboy2Compat &engine() { return portState().engine.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_engine = &state->engine;
    g_game = &state->game;
    g_intro = &state->intro;
    g_flash = &state->flash;
    g_death = &state->death;
    g_state = &state->state;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_engine = nullptr;
    g_game = nullptr;
    g_intro = nullptr;
    g_flash = nullptr;
    g_death = nullptr;
    g_state = nullptr;
}
} // namespace SpaceTanksSandbox

#undef FRAMERATE
#undef SCREEN_WIDTH
#undef SCREEN_HEIGHT
#undef GAME_STATE_BANNER
#undef GAME_STATE_INTRO
#undef GAME_STATE_START
#undef GAME_STATE_PLAYING
#undef GAME_STATE_DEAD
#undef MAX_FLASHING_MESSAGE_LENGTH
#undef FLASHING_MESSAGE_TEXT_SIZE
#undef FLASHING_MESSAGE_TEXT_H
#undef FLASHING_MESSAGE_TEXT_W
#undef INTRO_MESSAGE_COUNT
#undef INTRO_MESSAGE_DURATION

#pragma pop_macro("FPS")
#pragma pop_macro("STATE_GAME_OVER")
#pragma pop_macro("STATE_TITLE")
