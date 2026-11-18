#pragma once

// Actual james-b-collins/AsteroidDodge-Arduboy source, isolated in
// AsteroidDodgeSandbox. The upstream game logic, rules and primitive-drawn
// assets are kept; only the AVR display/input/audio/EEPROM boundary is replaced
// by the shared compatibility layer and a local RAM-EEPROM view.
//
// Every upstream constant that could collide with a macro from Castle Boy or a
// neighbouring sketch is pushed, undefined for the include, then restored.
#pragma push_macro("FPS")
#pragma push_macro("SCREEN_W")
#pragma push_macro("SCREEN_H")
#pragma push_macro("PLAYER_SPEED")
#pragma push_macro("PLAYER_W")
#pragma push_macro("PLAYER_H")
#pragma push_macro("LEVEL_SEED")
#pragma push_macro("EEPROM_HIGHSCORE_ADDR")
#pragma push_macro("EEPROM_MAGIC_ADDR")
#pragma push_macro("EEPROM_MAGIC")
#pragma push_macro("MAX_ASTEROIDS")
#pragma push_macro("ASTEROID_W")
#pragma push_macro("ASTEROID_H")
#pragma push_macro("SPAWN_PERIOD")
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_ABOUT")
#pragma push_macro("STATE_PLAY")
#pragma push_macro("STATE_DEAD")
#pragma push_macro("STATE_GAMEOVER")
#pragma push_macro("TONES_END")
#pragma push_macro("EEPROM")
#undef FPS
#undef SCREEN_W
#undef SCREEN_H
#undef PLAYER_SPEED
#undef PLAYER_W
#undef PLAYER_H
#undef LEVEL_SEED
#undef EEPROM_HIGHSCORE_ADDR
#undef EEPROM_MAGIC_ADDR
#undef EEPROM_MAGIC
#undef MAX_ASTEROIDS
#undef ASTEROID_W
#undef ASTEROID_H
#undef SPAWN_PERIOD
#undef STATE_TITLE
#undef STATE_ABOUT
#undef STATE_PLAY
#undef STATE_DEAD
#undef STATE_GAMEOVER
#undef TONES_END
#undef EEPROM

namespace AsteroidDodgeSandbox {

using Arduboy2 = Arduboy2Compat;

// This port's upstream arrays end with its own TONES_END == 0 sentinel, not
// ArduboyTones' 0x8000. State that explicitly instead of guessing.
class AsteroidDodgeTones : public ArduboyTonesCompat {
public:
    explicit AsteroidDodgeTones(bool (*enabled)() = nullptr)
        : ArduboyTonesCompat(enabled) {}
    void tones(const uint16_t *pairs) {
        if (!CrabitArduboyAudio::enabled()) return;
        CrabitArduboyAudio::playTones(pairs,
                                      CrabitArduboyAudio::TerminatorZero,
                                      CrabitArduboyAudio::PrioritySequence);
    }
};

using ArduboyTones = AsteroidDodgeTones;

#define TONES_END 0

// Upstream used the Arduino EEPROM library. This view maps get()/put() onto the
// active compat engine's bounded 1024-byte RAM block; no real Flash is touched.
class AsteroidDodgeEeprom {
public:
    void attach(Arduboy2Compat *engine) { target = engine; }

    template <class T>
    void get(int address, T &value) const {
        uint8_t *out = reinterpret_cast<uint8_t *>(&value);
        Arduboy2Compat *engine = target ? target : g_arduboyCompatActive;
        for (unsigned i = 0; i < sizeof(T); ++i)
            out[i] = engine ? engine->eeprom.read(address + int(i)) : 0xFF;
    }

    template <class T>
    void put(int address, const T &value) {
        const uint8_t *in = reinterpret_cast<const uint8_t *>(&value);
        Arduboy2Compat *engine = target ? target : g_arduboyCompatActive;
        if (!engine) return;
        for (unsigned i = 0; i < sizeof(T); ++i)
            engine->eeprom.update(address + int(i), in[i]);
    }

private:
    Arduboy2Compat *target = nullptr;
};

AsteroidDodgeEeprom EEPROM;

#include "asteroiddodge.inc.h"

/*  On-demand port state                                               */

#undef TONES_END
#undef arduboy
#undef sound
#undef state
#undef player
#undef asteroids
#undef menuSelection
#undef score
#undef highScore
#undef frameCount
#undef spawnTimer
#undef deathTimer
#undef lives
#undef invincTimer

struct State {
    GameStateHolder game;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().game.arduboy; }

// Called before setup() on every mount. The RAM-EEPROM high score deliberately
// survives visits; all transient game state returns to the original title.
void resetGameState() {
    State &s = portState();
    s.game.state = STATE_TITLE;
    s.game.menuSelection = 0;
    s.game.score = 0;
    s.game.frameCount = 0;
    s.game.spawnTimer = 0;
    s.game.deathTimer = 0;
    s.game.lives = 3;
    s.game.invincTimer = 0;
    ResetAsteroids();
    for (uint8_t i = 0; i < MAX_ASTEROIDS; ++i) {
        s.game.asteroids[i].x = 0;
        s.game.asteroids[i].y = 0;
        s.game.asteroids[i].vx = 0;
    }
    s.game.player.x = SCREEN_W - PLAYER_W - 20;
    s.game.player.y = SCREEN_H / 2;
}

inline void installPortState(State *state) {
    g_portState = state;
    g_game = &state->game;
    EEPROM.attach(&state->game.arduboy);
}

inline void clearPortState() {
    g_portState = nullptr;
    g_game = nullptr;
    EEPROM.attach(nullptr);
}
} // namespace AsteroidDodgeSandbox

#pragma pop_macro("EEPROM")
#pragma pop_macro("TONES_END")
#pragma pop_macro("STATE_GAMEOVER")
#pragma pop_macro("STATE_DEAD")
#pragma pop_macro("STATE_PLAY")
#pragma pop_macro("STATE_ABOUT")
#pragma pop_macro("STATE_TITLE")
#pragma pop_macro("SPAWN_PERIOD")
#pragma pop_macro("ASTEROID_H")
#pragma pop_macro("ASTEROID_W")
#pragma pop_macro("MAX_ASTEROIDS")
#pragma pop_macro("EEPROM_MAGIC")
#pragma pop_macro("EEPROM_MAGIC_ADDR")
#pragma pop_macro("EEPROM_HIGHSCORE_ADDR")
#pragma pop_macro("LEVEL_SEED")
#pragma pop_macro("PLAYER_H")
#pragma pop_macro("PLAYER_W")
#pragma pop_macro("PLAYER_SPEED")
#pragma pop_macro("SCREEN_H")
#pragma pop_macro("SCREEN_W")
#pragma pop_macro("FPS")
