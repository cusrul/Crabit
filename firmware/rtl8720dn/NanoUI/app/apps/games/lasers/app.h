#ifndef APP_LASERS_H
#define APP_LASERS_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma push_macro("STATE_TITLE")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef FPS
namespace LasersSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
#include "MyArduboy.h"
#include "common.h"
#include "MyArduboy.inc.h"
#include "common.inc.h"
#include "menu.inc.h"
#include "logo.inc.h"
#include "warn.inc.h"
// Preserve the original per-translation-unit title/game state separation.
#define STATE_T TITLE_STATE_T
#define STATE_INIT TITLE_STATE_INIT
#define state titleState
#include "title.inc.h"
#undef state
#undef STATE_INIT
#undef STATE_T
#include "game.inc.h"

void resetPortState() {
    memset(&record, 0, sizeof(record));
    isRecordDirty = false;
    lastScore = 0;
    isInvalid = false;
    recordState = RECORD_NOT_READ;
    eepAddr = 0;
    grayBlink = true;
    // RAM records are intentionally fresh on each mount, never real Flash.
    for (int i = 0; i < 1024; ++i) arduboy.eeprom.update(i, 0xff);
    menuX = menuY = menuW = menuH = menuItemCount = menuItemPos = 0;
    memset(menuItemAry, 0, sizeof(menuItemAry));
    counter = 0;
    signalOn = false;
    p = nullptr;
    x = y = 0;
    titleState = TITLE_STATE_INIT;
    memset(lines, 0, sizeof(lines));
    state = STATE_INIT;
    memset(lasers, 0, sizeof(lasers));
    memset(sparks, 0, sizeof(sparks));
    score = gameFrames = 0;
    laserCount = 0;
    power = playerX = playerY = laserWait = 0;
    playerVx = playerVy = playerMoving = colorBias = 0;
    scoreX = scoreY = powerY = hitSound = lastHitSound = sparkIdx = 0;
    isPlayerWhite = isHiscore = false;
    pSound = nullptr;
}

#include "lasers.inc.h"
} // namespace LasersSandbox

// Preprocessor names are not scoped by the sandbox namespace.
#undef APP_CODE
#undef APP_RELEASED
#undef APP_TITLE
#undef APP_VERSION
#undef COUNTER_MAX
#undef DAMAGE_LASER
#undef DEG_COEF
#undef DEG_MAX
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE
#undef FPS
#undef IMG_LASER_DIV
#undef IMG_LASER_ID_MAX
#undef LASERS_MAX
#undef LASER_DIAM
#undef LASER_WAIT_MAX
#undef LASER_WAIT_MIN
#undef LASER_X_MAX
#undef LINES_MAX
#undef LINE_VX_MAX
#undef LINE_VX_MIN
#undef LINE_X_MAX
#undef MENU_COUNT_MAX
#undef PLAYER_DIAM
#undef PLAYER_X_MAX
#undef PLAYER_X_MIN
#undef PLAYER_Y_MAX
#undef PLAYER_Y_MIN
#undef POWER_MAX
#undef RAY_R_DEC
#undef RAY_R_INC
#undef RAY_R_MAX
#undef RAY_VD_MAX
#undef RAY_VD_MIN
#undef SCALE
#undef SCALEF
#undef SIGNAL_PTN
#undef SPARKS_HIT
#undef SPARKS_MAX
#undef SPARK_D_DEVI
#undef SPARK_R_MIN
#undef SPARK_S_MIN
#undef SPARK_S_MIN_EX
#undef Y_MAX
#undef callDrawFunc
#undef callInitFunc
#undef callUpdateFunc
#undef descale
#undef dprint
#undef dprintln
#pragma pop_macro("FPS")
#pragma pop_macro("STATE_TITLE")

class LasersApp : public ArduboyCompatApp {
public:
    LasersApp() { port_ptr = &LasersSandbox::arduboy; }
    const char* getAppName() override { return "Lasers"; }
protected:
    // B is the frequent in-game key (polarity toggle) and also confirms the
    // menu, accepts the flicker warning and retries. Single PAT tap = B,
    // double tap = A (pause/leave).
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    void game_setup() override { LasersSandbox::setup(); }
    void game_loop() override { LasersSandbox::loop(); }
};

#endif
