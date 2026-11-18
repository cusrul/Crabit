#pragma once

// Included once by app.h. The original LongCat tree stays private in
// LongCatSandbox. The compatibility layer supplies Arduboy2, the framebuffer,
// buttons and silent audio; local shims below restore the static-style
// Sprites/ArduboyTones APIs and route EEPROM to the engine's private RAM.
#pragma push_macro("FPS")
#pragma push_macro("STATE_TITLE")
#undef FPS
#undef STATE_TITLE

namespace LongCatSandbox {

using Arduboy2 = Arduboy2Compat;

// SaveUtil addresses the private RAM EEPROM through these two helpers so the
// shim never depends on a global "last engine" pointer.
uint8_t longCatEepromRead(int address);
void longCatEepromWrite(int address, uint8_t value);

// The original source calls Sprites::draw* statically. Declared first so the
// inline CatChars/LevelRenderer bodies can see it, defined after `game`.
class Sprites {
public:
    static void drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawErase(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
    static void drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame);
};

// Original ArduboyTones use is static; both call sites go through the shared
// mono adapter (registered now, emitted on the App audio tick).
class ArduboyTones {
public:
    explicit ArduboyTones(bool (*)() = nullptr) {}
    static void tone(uint16_t frequency, uint16_t durationMs) {
        if (CrabitArduboyAudio::enabled())
            CrabitArduboyAudio::playTone(frequency, durationMs,
                                         CrabitArduboyAudio::PriorityEffect);
    }
    static void tone(uint16_t frequency1, uint16_t duration1,
                     uint16_t frequency2, uint16_t duration2) {
        if (!CrabitArduboyAudio::enabled()) return;
        const uint16_t pairs[4] = {frequency1, duration1, frequency2, duration2};
        CrabitArduboyAudio::playTonePairs(pairs, 2,
                                         CrabitArduboyAudio::PriorityEffect);
    }
};

class EepromCompat {
public:
    template <class T> void get(int address, T &value) {
        uint8_t *bytes = reinterpret_cast<uint8_t *>(&value);
        for (size_t i = 0; i < sizeof(T); ++i)
            bytes[i] = longCatEepromRead(address + int(i));
    }
    template <class T> void put(int address, const T &value) {
        const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&value);
        for (size_t i = 0; i < sizeof(T); ++i)
            longCatEepromWrite(address + int(i), bytes[i]);
    }
};
EepromCompat EEPROM;

// The complete original tree, with .cpp bodies renamed .inc.h so Arduino
// staging does not compile them separately.
#include "src/Game.h"
#include "src/Game.inc.h"
#include "src/Utils/SaveUtil.inc.h"
#include "src/Utils/LevelUtils.inc.h"
#include "src/Utils/LevelRenderer.inc.h"
#include "src/States/GameStateSplashScreen.inc.h"
#include "src/States/GameStateMainMenu.inc.h"
#include "src/States/GameStateCampaignMenu.inc.h"
#include "src/States/GameStateRandomMenu.inc.h"
#include "src/States/GameStateOptionsMenu.inc.h"
#include "src/States/GameStateCredits.inc.h"
#include "src/States/GameStateSelectLevel.inc.h"
#include "src/States/GameStateSelectSeed.inc.h"
#include "src/States/GameStateLoadLevel.inc.h"
#include "src/States/GameStateGamePlay.inc.h"
#include "src/States/GameStateSimulate.inc.h"
#include "src/States/GameStateEndScreen.inc.h"

/*  On-demand port state: the original Game object embeds the engine.  */

struct State {
    Game game;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().game.getArduboy(); }

#define game (portState().game)

inline void installPortState(State *state) {
    g_portState = state;
}

inline void clearPortState() {
    g_portState = nullptr;
}


uint8_t longCatEepromRead(int address)
{
    return game.getArduboy().eeprom.read(address);
}

void longCatEepromWrite(int address, uint8_t value)
{
    game.getArduboy().eeprom.write(address, value);
}

void Sprites::drawOverwrite(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
    SpritesCompat sprites(game.getArduboy());
    sprites.drawOverwrite(x, y, bitmap, frame);
}

void Sprites::drawErase(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
    SpritesCompat sprites(game.getArduboy());
    sprites.drawErase(x, y, bitmap, frame);
}

void Sprites::drawSelfMasked(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t frame)
{
    SpritesCompat sprites(game.getArduboy());
    sprites.drawSelfMasked(x, y, bitmap, frame);
}

// Engine input/pacing/canvas reset belongs to ArduboyCompatApp. The RAM EEPROM
// save intentionally survives visits; all other state is rebuilt here.
void resetGameState()
{
    game.resetState();
}

#undef game
} // namespace LongCatSandbox

#undef CatKeyLength

#pragma pop_macro("STATE_TITLE")
#pragma pop_macro("FPS")
