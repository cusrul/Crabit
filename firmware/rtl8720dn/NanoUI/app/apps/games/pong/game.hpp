#ifndef CRABIT_PONG_GAME_HPP
#define CRABIT_PONG_GAME_HPP

#include "ball.hpp"
#include "paddle.hpp"

#define WALL_FREQ   NOTE_A3
#define PADDLE_FREQ NOTE_A4
#define POINT_FREQ  NOTE_B4
#define WALL_DUR    16
#define PADDLE_DUR  96
#define POINT_DUR   257

extern Arduboy2 arduboy; // found in pong.ino
extern ArduboyTones sound; // found in pong.ino

extern Ball ball;
extern Player1 player1;
extern Player2 player2;

void tick();

// true if the game is being played with 2 players, false if just 1
bool twoPlayerGame();

#endif // CRABIT_PONG_GAME_HPP
