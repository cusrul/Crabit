#ifndef APP_HOLLOW_SEEKER_H
#define APP_HOLLOW_SEEKER_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace HollowSeekerSandbox {
using ArduboyPortMath::abs;
using ArduboyPortMath::min;
using ArduboyPortMath::max;
// AVR rand() is 15-bit; host/ARM RAND_MAX must not alter cave generation.
static uint32_t randomState = 1;
static int rand() {
    randomState = static_cast<uint32_t>((static_cast<uint64_t>(randomState) * 16807U) % 2147483647U);
    return randomState & 0x7FFF;
}
static void randomSeed(uint32_t seed) {
    if (seed) { randomState = seed % 2147483647U; if (!randomState) randomState = 1; }
}
static void resetPortState();

#include "MyArduboy.h"
#include "MyArduboy.inc.h"
#include "common.h"
#include "logo.inc.h"
#include "title.inc.h"
#include "game.inc.h"
#include "hollow.inc.h"

static void resetPortState() {
    randomState = 1;
    mode = LOGO_MODE;
    logoCounter = 0; signalOn = false;
    state = 0; toDraw = false; sound = false; menuPos = 0;
    lastScore = 0; recordState = RECORD_NOT_READ; eepAddr = 0;
    memset(hiScore, 0, sizeof(hiScore));
    playCount = 0; playFrames = 0;
    eyesWait = 0; lightColor = 0; lightBlink = false; lightWait = 0;
    isStart = false; isOver = false; score = 0; scoreTop = 0;
    isHiscore = false; counter = 0;
    caveOffset = 0; cavePhase = 0; caveHollowCnt = 0; caveLoopCnt = 0;
    caveMaxGap = 0; caveGap = 0; caveBaseTop = 0; caveBaseBottom = 0;
    memset(caveColumn, 0, sizeof(caveColumn));
    playerX = 0; playerColumn = 0; playerMove = 0; playerDir = false; playerJump = 0;
    memset(debris, 0, sizeof(debris));
}
} // namespace HollowSeekerSandbox

// Do not leak the upstream sketch's private macros into other game ports.
#undef APP_TITLE
#undef APP_CODE
#undef APP_VERSION
#undef APP_RELEASED
#undef rnd
#undef mod
#undef SIGNAL_PTN
#undef STATE_MENU
#undef STATE_RECORD
#undef STATE_CREDIT
#undef RECORD_NOT_READ
#undef RECORD_INITIAL
#undef RECORD_STORED
#undef EEPROM_ADDR_BASE
#undef EEPROM_SIGNATURE

class HollowSeekerApp : public ArduboyCompatApp {
protected:
    void game_setup() override { HollowSeekerSandbox::setup(); }
    void game_loop() override { HollowSeekerSandbox::loop(); }
public:
    const char* getAppName() override { return "HOLLOW SEEKER"; }
    HollowSeekerApp() {
        port_ptr = &HollowSeekerSandbox::arduboy;
    }
};
#endif
