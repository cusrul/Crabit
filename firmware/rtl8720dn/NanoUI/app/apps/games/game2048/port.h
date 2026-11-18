#pragma once

// Actual awisse/2048-Arduboy source, isolated in Game2048Sandbox.
// The original AVR/Arduboy hardware calls are provided by the shared compat
// layer; EEPROM is the shared engine's private RAM block.
#pragma push_macro("Arduboy2Base")
#pragma push_macro("EEPROM_STORAGE_SPACE_START")
#pragma push_macro("EEP")
#pragma push_macro("DIM")
#pragma push_macro("PLACES")
#pragma push_macro("FRAME_RATE")
#pragma push_macro("FRAME_STEP")
#pragma push_macro("MAX_STARS")
#pragma push_macro("REWARD_TIME")
#pragma push_macro("XPOS")
#pragma push_macro("YPOS")
#pragma push_macro("MAX_VALUE")
#pragma push_macro("TARGET_VALUE")
#pragma push_macro("HIPROB")
#pragma push_macro("DISPLAY_WIDTH")
#pragma push_macro("DISPLAY_HEIGHT")
#pragma push_macro("BOARD_X")
#pragma push_macro("BOARD_Y")
#pragma push_macro("TILE_SZ")
#pragma push_macro("SCORE_X")
#pragma push_macro("SCORE_LINE")
#pragma push_macro("HIGHSCORE_LINE")
#pragma push_macro("MAX_LINE")
#pragma push_macro("FONT_STEP")
#pragma push_macro("OVER_LINE")
#pragma push_macro("COLOUR_BLACK")
#pragma push_macro("COLOUR_WHITE")
#pragma push_macro("COLOUR_INVERT")
#pragma push_macro("FLASH_FRAMES")
#pragma push_macro("INPUT_LEFT")
#pragma push_macro("INPUT_RIGHT")
#pragma push_macro("INPUT_UP")
#pragma push_macro("INPUT_DOWN")
#pragma push_macro("INPUT_A")
#pragma push_macro("INPUT_B")
#pragma push_macro("LONG_PRESS")
#pragma push_macro("U8")
#pragma push_macro("U16")
#pragma push_macro("I16")
#pragma push_macro("DEV_DEB")
#undef Arduboy2Base
#define EEPROM_STORAGE_SPACE_START 16

namespace Game2048Sandbox {
typedef Arduboy2Compat Arduboy2Base;

// The upstream EEPROM library is replaced by a view over the active compat
// engine's bounded 1024-byte RAM EEPROM. No real EEPROM/Flash is touched.
class Game2048Eeprom {
public:
    void attach(Arduboy2Compat *engine) { target = engine; }
    uint8_t &operator[](int address) {
        static uint8_t dummy = 0xFF;
        Arduboy2Compat *engine = target ? target : g_arduboyCompatActive;
        if (!engine || address < 0 || address >= 1024) return dummy;
        return engine->eeprom.bytes[address];
    }
    uint16_t length() const { return 1024; }
private:
    Arduboy2Compat *target = nullptr;
};
Game2048Eeprom EEPROM;

#include "defines.h"
#include "globals.h"
#include "game.h"
#include "controller.h"
#include "draw.h"
#include "platform.h"
#include "font.h"
#include "stars.h"
#include "utils.h"
#include "controller.inc.h"
#include "draw.inc.h"
#include "font.inc.h"
#include "stars.inc.h"
#include "utils.inc.h"
#include "game.inc.h"
#include "game2048.inc.h"

/*  On-demand port state                                               */

struct State {
    EngineState engine;
    ControllerState controller;
    GamePlayState play;
    StarsState starfield;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }

void resetGameState() {
    started = false;
    memset(board, 0, sizeof(board));
    reached = 0;
    GameState = GameStateStruct();
    flash = 0;
    board_full = false;
    animating = false;
    reward_start = 0;
    ResetController();
}

#undef arduboy
#undef started
#undef previousButtons
#undef currentButtons
#undef startAPress
#undef startBPress
#undef AButtonDown
#undef BButtonDown
#undef BButtonLongPressCycles
#undef board
#undef reached
#undef GameState
#undef flash
#undef board_full
#undef animating
#undef reward_start
#undef stars

inline Arduboy2Compat &engine() { return portState().engine.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_engine = &state->engine;
    g_controller = &state->controller;
    g_play = &state->play;
    g_stars = &state->starfield;
    EEPROM.attach(&state->engine.arduboy);
}

inline void clearPortState() {
    g_portState = nullptr;
    g_engine = nullptr;
    g_controller = nullptr;
    g_play = nullptr;
    g_stars = nullptr;
    EEPROM.attach(nullptr);
}
} // namespace Game2048Sandbox

#undef EEP
#undef EEPROM_STORAGE_SPACE_START
#undef Arduboy2Base
#define Arduboy2Base Arduboy2Compat

#pragma pop_macro("DEV_DEB")
#pragma pop_macro("I16")
#pragma pop_macro("U16")
#pragma pop_macro("U8")
#pragma pop_macro("LONG_PRESS")
#pragma pop_macro("INPUT_B")
#pragma pop_macro("INPUT_A")
#pragma pop_macro("INPUT_DOWN")
#pragma pop_macro("INPUT_UP")
#pragma pop_macro("INPUT_RIGHT")
#pragma pop_macro("INPUT_LEFT")
#pragma pop_macro("FLASH_FRAMES")
#pragma pop_macro("COLOUR_INVERT")
#pragma pop_macro("COLOUR_WHITE")
#pragma pop_macro("COLOUR_BLACK")
#pragma pop_macro("OVER_LINE")
#pragma pop_macro("FONT_STEP")
#pragma pop_macro("MAX_LINE")
#pragma pop_macro("HIGHSCORE_LINE")
#pragma pop_macro("SCORE_LINE")
#pragma pop_macro("SCORE_X")
#pragma pop_macro("TILE_SZ")
#pragma pop_macro("BOARD_Y")
#pragma pop_macro("BOARD_X")
#pragma pop_macro("DISPLAY_HEIGHT")
#pragma pop_macro("DISPLAY_WIDTH")
#pragma pop_macro("HIPROB")
#pragma pop_macro("TARGET_VALUE")
#pragma pop_macro("MAX_VALUE")
#pragma pop_macro("YPOS")
#pragma pop_macro("XPOS")
#pragma pop_macro("REWARD_TIME")
#pragma pop_macro("MAX_STARS")
#pragma pop_macro("FRAME_STEP")
#pragma pop_macro("FRAME_RATE")
#pragma pop_macro("PLACES")
#pragma pop_macro("DIM")
#pragma pop_macro("EEP")
#pragma pop_macro("EEPROM_STORAGE_SPACE_START")
#pragma pop_macro("Arduboy2Base")
