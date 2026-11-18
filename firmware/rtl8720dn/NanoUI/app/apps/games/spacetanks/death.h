#pragma once

#include "globals.h"
#include "utils.h"

struct DeathState {
    uint8_t deathFrameCount = 0;
    char deathMessage[12] = "YOU LOSER";
};
static DeathState *g_death = nullptr;
#define deathFrameCount (g_death->deathFrameCount)
#define deathMessage (g_death->deathMessage)

bool isShowingDeathMessage() {
  if (deathFrameCount == 0) {
    startFlashingMessage(deathMessage, 2 * FRAMERATE);
  }

  drawFlashingMessage();

  deathFrameCount++;

  if (deathFrameCount > 3 * FRAMERATE) {
    return false;
  }

  return true;
}
