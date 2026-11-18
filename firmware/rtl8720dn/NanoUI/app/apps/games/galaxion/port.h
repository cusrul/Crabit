#pragma once

// Included once by app.h. The original Galaxion translation units are isolated
// under GalaxionSandbox; this boundary also hides/restores Castle Boy macros
// that precede it in the combined sketch TU.
//
// Resource model: the engine, the Game object, the star field and every
// dynamically created enemy/explosion/boss-score/ship-missile now live in one
// typed State created on demand (see ArduboyPortRuntime.h). The original code
// used new/delete; those become bounded slots owned by the State so a failed
// launch or re-entry cannot leak, and their peak is included in the State cost.
#pragma push_macro("STATE_TITLE")
#pragma push_macro("STATE_GAME_OVER")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef STATE_GAME_OVER
#undef FPS

namespace GalaxionSandbox {
#include "common.h"
#include "game.h"

struct GalaxionStar {
    int8_t speed;
    int8_t x;
    int16_t y;
};

// All mutable port state. Plain scalars keep the old static-storage guarantee
// because createPortState value-initializes the State.
struct State {
    Arduboy mArduboy;
    Game mGame;
    GalaxionStar star[20];

    Enemy enemySlots[MAX_ENEMIES];
    bool enemyUsed[MAX_ENEMIES];
    Explosion explosionSlot;
    BossScore bossScoreSlot;
    Position shipMissileSlot;

    // Zero-save declaration guard (host regression only).
    uint16_t unexpectedEepromAccess;
};

// Stable per-port handle: exactly one live State while the App exists; cleared
// before destruction and never allocated during static initialization.
static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline void installPortState(State *state) { g_portState = state; }
inline void clearPortState() { g_portState = nullptr; }
inline Arduboy2Compat &engine() { return portState().mArduboy; }

// Powered-session auto-fire preference, ON by default. Like the RAM high score
// it survives leaving and re-entering the port and is lost on power-off; it is
// deliberately not cleared by resetGameState(), so re-entry cannot silently
// switch fire back on after the player chose OFF. One flag, not engine or game
// state: the ship still needs its real GAME_ST_PLAY state, so this alone never
// fires from the title, the stage entry or the game-over screen.
inline bool &autoFirePreference() {
    static bool enabled = true;
    return enabled;
}

// Bounded replacements for the original dynamic game objects.
inline Enemy *acquireEnemy() {
    State &state = portState();
    for (int idx = 0; idx < MAX_ENEMIES; ++idx) {
        if (!state.enemyUsed[idx]) {
            state.enemyUsed[idx] = true;
            state.enemySlots[idx] = Enemy();
            return &state.enemySlots[idx];
        }
    }
    return nullptr; // every slot busy: caller already reports failure
}
inline void releaseEnemy(Enemy *enemy) {
    State &state = portState();
    for (int idx = 0; idx < MAX_ENEMIES; ++idx) {
        if (enemy == &state.enemySlots[idx]) {
            state.enemyUsed[idx] = false;
            return;
        }
    }
}
inline Explosion *acquireExplosion() {
    State &state = portState();
    state.explosionSlot = Explosion();
    return &state.explosionSlot;
}
inline void releaseExplosion(Explosion *explosion) {
    (void)explosion; // single slot; the caller clears its pointer
}
inline BossScore *acquireBossScore() {
    State &state = portState();
    state.bossScoreSlot = BossScore();
    return &state.bossScoreSlot;
}
inline void releaseBossScore(BossScore *score) {
    (void)score;
}
inline Position *acquireShipMissile(int x, int y) {
    State &state = portState();
    state.shipMissileSlot = Position(x, y);
    return &state.shipMissileSlot;
}
inline void releaseShipMissile(Position *missile) { (void)missile; }

// Unqualified upstream references resolve to the live State.
#define mArduboy (portState().mArduboy)
#define mGame (portState().mGame)
#define star (portState().star)

// Auto fire only acts inside the real battle state; menus, the five-second
// stage entry and the game-over screen stay manual. This reads the game state
// that owns the ship, so it cannot leak into a non-battle screen.
inline bool autoFireBattleActive() {
    return autoFirePreference() && mGame.mState == GAME_ST_PLAY;
}

#include "galaxion.inc.h"
#include "ship.inc.h"
#include "enemy.inc.h"
#include "game.inc.h"

#undef star
#undef mGame
#undef mArduboy

void resetGameState()
{
    portState().mGame.reset();
}
} // namespace GalaxionSandbox

#undef FRAMES_IN_SECOND
#undef SCREEN_X
#undef SCREEN_W
#undef POS_SCALE
#undef CONVOY_COLS
#undef CONVOY_ROWS
#undef MAX_ENEMIES
#undef MAX_MISSILES
#undef CAPTURE_SCREEN

#pragma pop_macro("FPS")
#pragma pop_macro("STATE_GAME_OVER")
#pragma pop_macro("STATE_TITLE")
