#ifndef CRABIT_WATERNET_PORT_H
#define CRABIT_WATERNET_PORT_H

// Actual joyrider3774/waternet_arduboy source, isolated in WaternetSandbox.
// The real 25-level Net/Netslide Rotate/Slide/Roslid rules, procedural maze
// generator, level-select/save state, help screens and 8x8 tile art are kept.
// Only the AVR display/input/audio/EEPROM boundary is replaced by the shared
// layer; the three RLE title/congrats streams are pre-decoded into exact page
// bitmaps in the local asset headers.
//
// Several older ports in this combined translation unit define short generic
// macros. Save and clear them here so including this game cannot inherit one.
#pragma push_macro("min")
#pragma push_macro("max")
#pragma push_macro("abs")
#pragma push_macro("random")
#pragma push_macro("randomSeed")
#pragma push_macro("bitToggle")
#pragma push_macro("CHEAT_MODE")
#undef min
#undef max
#undef abs
#undef random
#undef randomSeed
#undef bitToggle
#undef CHEAT_MODE

namespace WaternetSandbox {

using ArduboyTones = ArduboyTonesCompat;

#define CRABIT_WATERNET_ACTIVE 1

#include "waternet.inc.h"

#undef CRABIT_WATERNET_ACTIVE

// Called before setup() on every mount. This clears transient gameplay state
// only; the RAM-only level-unlock map and audio option bits stay in place for
// the lifetime of the app object, exactly like the upstream EEPROM save.
void resetGameState() {
    option = 0;
    difficulty = diffNormal;
    selectedLevel = 1;
    mainMenu = mmStartGame;
    gameState = gsInitIntro;
    titleStep = tsMainMenu;
    gameMode = gmRotate;
    posAdd = 0;
    selectionX = selectionY = 0;
    moves = 0;
    levelDone = 0;
    needRedraw = 0;
    paused = 0;
    wasMusicOn = wasSoundOn = 0;
    redrawLevelDoneBit = 0;
    startPos = boardX = boardY = 0;
    boardWidth = boardHeight = boardSize = 0;
    maxLevel = levelCount;
    cursorFrameCount = cursorFrame = showCursor = 0;
    memset(level, 0, sizeof(level));
    for (uint8_t i = 0; i < cursorNumTiles; ++i)
        spritePos[i][0] = spritePos[i][1] = 0;
    // Engine-owned frame/button state is reset by ArduboyCompatApp::onMount.
}

} // namespace WaternetSandbox

#pragma pop_macro("CHEAT_MODE")
#pragma pop_macro("bitToggle")
#pragma pop_macro("randomSeed")
#pragma pop_macro("random")
#pragma pop_macro("abs")
#pragma pop_macro("max")
#pragma pop_macro("min")

#endif
