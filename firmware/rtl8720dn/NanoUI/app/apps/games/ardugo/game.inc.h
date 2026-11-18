// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "ArduGo.h"
#include "Board.h"
#include "sidebar.h"

/*
 *  File: go-game.ino
 *  Purpose: Implementation of Arduino entry-points.
 *  Author: Don Anderson
 *
 *  Port note: the debug shell and Serial setup are excluded. The shared
 *  adapter drives one call per display frame; nextFrame() keeps the original
 *  frame pacing/button sampling and never blocks other NanoUI services.
 */

void setup() {

  arduboy.start();
  ardugo_setup();

}

void loop() {
  if (!arduboy.nextFrame()) return;

  previousButtonState = currentButtonState;
  currentButtonState = arduboy.buttonsState();

  board.loop();
  arduboy.clear();
  board.render();
  render_sidebar(cur_sidebar);
  arduboy.display();

  injectButtonState = 0;
}
