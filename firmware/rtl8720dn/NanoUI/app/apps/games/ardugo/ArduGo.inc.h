// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "ArduGo.h"
#include "Board.h"
#include "sidebar.h"
#include "assets.h"

/*
 *  File: ArduGo.cpp
 *  Purpose: Implementation of game-wide assets.
 *  Author: Don Anderson
 *
 *  Port note: the debug serial shell/MemoryFree files are not compiled. The
 *  original random seed used time(NULL); no RTC is required here, so the seed
 *  is taken from millis() at setup.
 */

// Small per-run scalars referenced from inline helpers that are parsed before
// this body; keeping them as namespace globals costs a few bytes and avoids
// reordering the upstream headers. The engine, tune stub and 19x19 board move
// to the on-demand State.
uint8_t previousButtonState = 0;
uint8_t currentButtonState = 0;
uint8_t injectButtonState = 0;
uint8_t black_live = 0, white_live = 0;
uint16_t black_capt = 0, white_capt = 0;

struct CommonState {
    ArduGoArduboy arduboy;
    ArduGoTunes tunes;
    Board board;
};
static CommonState *g_common = nullptr;
#define arduboy (g_common->arduboy)
#define tunes (g_common->tunes)
#define board (g_common->board)

void
injectButton(uint8_t button){
  injectButtonState |= button;
}

bool_t
wasJustPressed(uint8_t button){
 return  (injectButtonState & button) ||
         (!(previousButtonState & button) && (currentButtonState & button))
         ? T : F;
}

void
ardugo_setup(){
  srand ((unsigned) millis());
  board.clear();
}

// The upstream bitmap helper took a non-const pointer, which only compiled
// with -fpermissive because the PROGMEM asset is const. Keep the same layout
// (two size bytes followed by the mono bitmap) with a const-correct pointer.
void
drawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap){
  arduboy.drawBitmap(x, y, bitmap+2, bitmap[0], bitmap[1] ,WHITE);
}
