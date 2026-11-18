#pragma once

#include "bitmaps/banner.h"
#include "game.h"
#include "globals.h"
#include "intro.h"
#include "utils.h"
#include "death.h"

#define GAME_STATE_BANNER  0
#define GAME_STATE_INTRO   1
#define GAME_STATE_START   2
#define GAME_STATE_PLAYING 3
#define GAME_STATE_DEAD    4

struct StateHolder {
    uint8_t gameState = GAME_STATE_BANNER;
};
static StateHolder *g_state = nullptr;
#define gameState (g_state->gameState)

void drawBanner() {
  arduboy.drawBitmap(0, 0, bannerFrames[0], screenWidth, screenHeight, WHITE);
}

void stateBanner() {
  // Auto fire is toggled here: the banner is the only screen without a battle.
  if (arduboy.justPressed(UP_BUTTON | DOWN_BUTTON)) {
    autoFirePreference() = !autoFirePreference();
  }

  // cock
  if (arduboy.pressed(A_BUTTON) || arduboy.pressed(B_BUTTON)) {
    initIntro();
    gameState = GAME_STATE_INTRO;
  }

  drawBanner();
  drawAutoFireSetting();
}

void stateIntro() {
  if (introFinished) {
    gameState = GAME_STATE_START;
  }

  drawIntro();
}

void stateStart() {
  arduboy.initRandomSeed();
  initGame();
  gameState = GAME_STATE_PLAYING;
}

void statePlaying() {
  handleInput();
  drawStarField();
  calculatePlayerCollision();
  drawShootyShootyBoom();
  drawBats();
  drawTank();
  drawScoreAndSevenYearsAgo();
  advanceEnemies();
  sweepAndSpawn();

  if (player.isDead) {
    gameState = GAME_STATE_DEAD;
  }
}

void stateDead() {
  if (!isShowingDeathMessage()) {
    gameState = GAME_STATE_BANNER;
  }
}

typedef void (*FunctionPointer) ();
const FunctionPointer PROGMEM mainGameLoop[] = {
  stateBanner,
  stateIntro,
  stateStart,
  statePlaying,
  stateDead,
};
