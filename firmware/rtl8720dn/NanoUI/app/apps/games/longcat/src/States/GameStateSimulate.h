#pragma once

#include <stdint.h>
#include "../GameContext/GameContext.h"
#include "../States/GameState.h"
#include "../Images/Bitmaps.h"
#include "../Map/Map.h"
#include "../Map/TileTypes.h"
#include "../Direction/Direction.h"
#include "../Utils/LevelRenderer.h"
#include "../Utils/LevelUtils.h"
#include "../Utils/SaveUtil.h"

// DO THIS WHEN HAVE TIME

class Game;
class GameStateSimulate
{
public:
  void update(Game &game);
  void render(Game &game);

private:
  // Original LevelRenderer::renderWin delay(2000); now a non-blocking hold.
  uint8_t winHold = 0;
  bool movePlayer(Game &game);
  void placeCatPiece(int8_t x, int8_t y, Game &game);
  void nextStage(Game &game);
};
