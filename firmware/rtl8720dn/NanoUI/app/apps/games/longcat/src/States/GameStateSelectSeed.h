#pragma once

#include <stdint.h>
#include "GameState.h"
#include "../Utils/RandKey.h"
#include "../Utils/CatChars.h"

// DO THIS WHEN HAVE TIME

class Game;
class GameStateSelectSeed
{
private:
  CatKey key = RandKey::toCatKey(0);
  uint8_t selection = 0;
public:
  void update(Game &game);
  void render(Game &game);
};
