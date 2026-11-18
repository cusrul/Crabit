#pragma once
#include "../Direction/Direction.h"

struct Hero
{
  int8_t x = 0;
  int8_t y = 0;
  int8_t dx = 0;
  int8_t dy = 0;
  Direction direction = Direction::Center;
  Direction lastDirection = Direction::Center;
};
