#pragma once

// Include-once body for the ARDUBULLETs port. Former upstream translation
// units keep their private static names inside per-module namespaces while all
// persistent state stays under ArdubulletsSandbox.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace ArdubulletsSandbox {
// Upstream uses Arduboy2's Point/Rect names; alias only inside this sandbox.
using CrabitArduboy::Point;
using CrabitArduboy::Rect;
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;

// Powered-session auto-fire preference, ON by default. Like the RAM record it
// survives leaving and re-entering the port and is lost on power-off; it is
// deliberately not part of resetPortState(), which clears transient gameplay,
// so re-entering cannot silently switch fire back on after the player chose
// OFF. It is one flag, not engine or game state; the fire site still requires
// the real battle state, so it alone can never fire from the title, the pause
// menu or the game-over sequence.
inline bool &autoFirePreference() {
    static bool enabled = true;
    return enabled;
}

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

#include "ardubullets.inc.h"

/*  On-demand port state                                                 */
/*                                                                       */
/*  The engine, the shared scalars and every module's statics live in one */
/*  typed State allocated before AppManager::launch. The macros used by   */
/*  the upstream bodies are dropped here so qualified initialization can  */
/*  use the same member names.                                            */

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
#undef mode

struct State {
    ArdubulletsEngineState engine;
    ArdubulletsCommonState common;
    ArdubulletsMenuState menu;
    Title::ArdubulletsTitleState title;
    Game::ArdubulletsGameState game;
    ArdubulletsModeState mode;
    // Audited zero-access guard is unused because this port keeps a real
    // record; the field stays for shape parity with other catalogue ports.
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }
inline Arduboy2Compat &engine() { return portState().engine.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_engine = &state->engine;
    g_common = &state->common;
    g_menu = &state->menu;
    Title::g_title = &state->title;
    Game::g_game = &state->game;
    g_mode = &state->mode;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_engine = nullptr;
    g_common = nullptr;
    g_menu = nullptr;
    Title::g_title = nullptr;
    Game::g_game = nullptr;
    g_mode = nullptr;
}

// Called once per mount, before setup(). RAM EEPROM deliberately survives
// visits (bounded records only); every other static is reset here so a second
// entry cannot inherit the previous game, menu or title sub-state.
void resetPortState() {
    State &s = portState();
    memset(&s.common.record, 0, sizeof(s.common.record));
    s.common.counter = 0;
    s.common.padX = s.common.padY = s.common.padRepeatCount = 0;
    s.common.isInvalid = s.common.isRecordDirty = false;
    s.common.recordState = RECORD_NOT_READ;
    s.common.eepAddr = 0;
    s.menu.menuX = s.menu.menuY = s.menu.menuW = s.menu.menuH = 0;
    s.menu.menuItemCount = s.menu.menuItemPos = 0;
    s.menu.isFramed = s.menu.isControlSound = s.menu.isInvalidMenu = false;
    memset(s.menu.menuItemAry, 0, sizeof(s.menu.menuItemAry));
    s.title.state = Title::STATE_INIT;
    s.title.gameSeedTmp = 0;
    s.title.gameSeedX = 0;
    s.title.isSettingChenged = false;
    s.game.state = Game::STATE_START;
    memset(s.game.shots, 0, sizeof(s.game.shots));
    memset(s.game.bullets, 0, sizeof(s.game.bullets));
    memset(s.game.groups, 0, sizeof(s.game.groups));
    memset(s.game.enemies, 0, sizeof(s.game.enemies));
    s.game.pShotCursor = s.game.shots;
    s.game.pBulletCursor = s.game.bullets;
    s.game.explo.x = s.game.explo.y = s.game.explo.a = 0;
    s.game.gameFrames = 0;
    s.game.gameRank = GAME_RANK_DEFAULT;
    s.game.bulletsNum = 0;
    s.game.playerX = s.game.playerY = s.game.playerFire = s.game.playerSlow = 0;
    s.game.ledLevel = s.game.ledRed = s.game.ledGreen = s.game.ledBlue = 0;
    s.game.isDefeated = s.game.isMenu = false;
    s.mode.mode = MODE_LOGO;
    s.engine.arduboy.setAudioEnabled(true);
}

// TEST-STATE-ACCESSORS
// Typed accessors for the live module State. The catalogue App owns
// it; nothing here allocates, and the pointers stay null until
// installPortState() runs.
namespace Title {
inline ArdubulletsTitleState &portState() { return *g_title; }
}
namespace Game {
inline ArdubulletsGameState &portState() { return *g_game; }
}

} // namespace ArdubulletsSandbox

// Preprocessor names are not scoped by the sandbox namespace.
#undef APP_CODE
#undef APP_RELEASED
#undef APP_TITLE
#undef APP_VERSION
#undef BULLET_BASE_SPD
#undef BULLET_CYC_BASE
#undef BULLET_GAP_BASE
#undef BULLET_MAX
#undef BULLET_SCALE
#undef BULLET_SPD_MAX
#undef BULLET_SPD_MIN
#undef COUNTER_MAX
#undef EDGE_THRESHOLD
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef ENEMY_ACTIVE
#undef ENEMY_FADE
#undef ENEMY_LIFE_INIT
#undef ENEMY_MAX
#undef ENEMY_UNITY
#undef FPS
#undef GAME_DURATION
#undef GAME_RANK_DEFAULT
#undef GAME_RANK_MAX
#undef GAME_SEED_MAX
#undef GAME_SEED_TOKEN_ALP
#undef GAME_SEED_TOKEN_MAX
#undef GAME_SEED_TOKEN_VAL
#undef GROUP_INT
#undef GROUP_MAX
#undef GROUP_TOTAL
#undef IMG_BUTTONS_H
#undef IMG_BUTTONS_W
#undef IMG_COMPLETED_H
#undef IMG_COMPLETED_W
#undef IMG_CRAFT_H
#undef IMG_CRAFT_W
#undef IMG_EXPLO_H
#undef IMG_EXPLO_ID_MAX
#undef IMG_EXPLO_W
#undef IMG_FAILED_H
#undef IMG_FAILED_W
#undef IMG_JETL_H
#undef IMG_JETL_W
#undef IMG_JETR_H
#undef IMG_JETR_W
#undef IMG_READY_H
#undef IMG_READY_W
#undef IMG_SOUNDM_W
#undef IMG_SOUND_W
#undef IMG_SPINNERS_H
#undef IMG_SPINNERS_W
#undef IMG_SUBTITLE_H
#undef IMG_SUBTITLE_W
#undef IMG_TITLE_H
#undef IMG_TITLE_W
#undef LED_IDX_OVER
#undef LED_LEVEL_NORMAL
#undef LED_LEVEL_OVER
#undef LED_VALUE
#undef MENU_COUNT_MAX
#undef OVER_OMIT
#undef PAD_REPEAT_DELAY
#undef PAD_REPEAT_INTERVAL
#undef PLAYER_FIRE_INT
#undef PLAYER_FIRE_MAX
#undef PLAYER_SCALE
#undef PLAYER_SLOW_MAX
#undef SHOT_MAX
#undef SHOT_SPD
#undef SIGNAL_PTN
#undef START_DURATION
#undef START_OMIT
#undef callDrawFunc
#undef callHandlerFunc
#undef callInitFunc
#undef callUpdateFunc
#undef circulate
#undef dprint
#undef dprintln
#undef myAtan2f
#undef myCos
