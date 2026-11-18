#pragma once

// Actual Hexagenic/minesweeper-arduboy source, isolated in its own namespace.
// Only the AVR display/input/audio boundary is replaced by the shared layer.
namespace MinesweeperSandbox {
class MinesweeperArduboy : public Arduboy2Compat {
public:
    void setTextColor(uint8_t color) {
        foreground = color;
        Arduboy2Compat::setTextColor(color);
    }
    void setTextColor(uint8_t color, uint8_t background) {
        foreground = color;
        Arduboy2Compat::setTextColor(color, background);
    }
    void setTextBackground(uint8_t background) {
        Arduboy2Compat::setTextColor(foreground, background);
    }
private:
    uint8_t foreground = WHITE;
};
typedef MinesweeperArduboy Arduboy2;

class MinesweeperPlaytune : public ArduboyPlaytuneCompat {
public:
    explicit MinesweeperPlaytune(bool (*)() = nullptr) {}
};

typedef MinesweeperPlaytune ArduboyPlaytune;

#define PIN_SPEAKER_1 0
#define PIN_SPEAKER_2 0
#include "minesweeper.inc.h"
#undef PIN_SPEAKER_1
#undef PIN_SPEAKER_2

struct State {
    CommonState common;
    uint16_t unexpectedEepromAccess;
};

static void *g_portState = nullptr;
inline State &portState() { return *static_cast<State *>(g_portState); }
inline State *portStatePtr() { return static_cast<State *>(g_portState); }

void resetGameState() {
    // The adapter owns engine/canvas/input reset. Reset the game's own statics
    // so a re-entry always begins with a clean, unrevealed board.
    gameState = GameState::Playing;
    memset(grid, 0, sizeof(grid));
    offsetX = offsetY = 0;
    markerX = markerY = 0;
    firstClick = true;
    pauseEnded = 0;
}

#undef arduboy
#undef tunes
#undef gameState
#undef grid
#undef offsetX
#undef offsetY
#undef markerX
#undef markerY
#undef firstClick
#undef pauseEnded

inline Arduboy2Compat &engine() { return portState().common.arduboy; }

inline void installPortState(State *state) {
    g_portState = state;
    g_common = &state->common;
}

inline void clearPortState() {
    g_portState = nullptr;
    g_common = nullptr;
}
} // namespace MinesweeperSandbox
