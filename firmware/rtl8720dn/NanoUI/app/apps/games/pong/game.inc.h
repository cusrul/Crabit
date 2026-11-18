#include "game.hpp"
#include "vector.hpp"

#define SCORE_MAX 3

// Read-only screen text. A named namespace-scope array keeps its bytes in a
// section named after the mangled symbol (`.rodata._ZN11PongSandbox...`), so
// the split linker attributes them to this game; an anonymous literal would be
// merged into .rodata.str1.1 and lose the namespace. `used` keeps the object
// emitted even though every reader is in this translation unit, and each array
// below is printed by the real menu paths.
__attribute__((used)) const char playersLabel[] = "players:";
__attribute__((used)) const char winText1[] = "Player 1\nwins!\nPress A to\nrestart";
__attribute__((used)) const char winText2[] = "Player 2\nwins!\nPress A to\nrestart";

// helper functions

static void
draw();

static void
resetBall();

// game state functions

static void
menuTitle();

static void
gameSetup();

static void
gameWait();

static void
gameMain();

static void
gamePause();

static void
menuWin1();

static void
menuWin2();

typedef void (*tick_func_t)();

void tick()
{
	gameTick();
}

bool twoPlayerGame()
{
	return twoPlayers;
}

void
draw()
{
	// cool border around the screen
	arduboy.drawRect(0, 0, WIDTH, HEIGHT, WHITE);
	// dotted line in the middle
	for (uint8_t i{ 2 }; i < HEIGHT; i += 8)
	{
		arduboy.drawFastVLine(WIDTH / 2, i, 4, WHITE);
	}
	// scores
	arduboy.setCursor(WIDTH/2 - 12, 2);
	arduboy.print(player1.getScore());
	arduboy.setCursor(WIDTH/2 + 3, 2);
	arduboy.print(player2.getScore());
	// objects
	ball.draw();
	player1.draw();
	player2.draw();
}

void
resetBall()
{
	ball.setPosition(Vector{ WIDTH / 2, HEIGHT / 2 });
	ball.setVelocity(Vector{ 1, 1 });
}

void
menuTitle()
{
	arduboy.setCursor(0, 0);
	arduboy.print(playersLabel);
	arduboy.setCursor(96, 0);
	arduboy.print(twoPlayers ? 2 : 1);
	if (arduboy.justPressed(LEFT_BUTTON) ||
		arduboy.justPressed(RIGHT_BUTTON))
	{
		twoPlayers = !twoPlayers;
	}
	if (arduboy.justPressed(A_BUTTON))
	{
		gameTick = &gameSetup;
	}
}

void
gameSetup()
{
	arduboy.initRandomSeed();
	resetBall();
	// set both paddles in the center of the vertical space
	player1.setPosition(Vector{ 9, 24 });
	player1.resetScore();
	player2.setPosition(Vector{ WIDTH - PADDLE_WIDTH - 9, 24 });
	player2.resetScore();
	draw();
	arduboy.display();
	setupWait = 30;
	gameTick = &gameWait;
}

void
gameWait()
{
	draw();
	if (setupWait > 0 && --setupWait == 0)
	{
		gameTick = &gameMain;
	}
}

void
gameMain()
{
	draw();
	// pause the game if needed
	if (arduboy.justPressed(LEFT_BUTTON))
	{
		gameTick = &gamePause;
		return;
	}
	ball.move();
	player1.move();
	player2.move();
	// check if player 1 scored
	if (ball.getPosition().getX() >= WIDTH - BALL_SIZE)
	{
		player1.updateScore();
		if (player1.getScore() >= SCORE_MAX)
		{
			gameTick = &menuWin1;
		}
		else
		{
			resetBall();
		}
		sound.tone(POINT_FREQ, POINT_DUR);
	}
	// check if player 2 scored
	else if (ball.getPosition().getX() < 1)
	{
		player2.updateScore();
		if (player2.getScore() >= SCORE_MAX)
		{
			gameTick = &menuWin2;
		}
		else
		{
			resetBall();
		}
		sound.tone(POINT_FREQ, POINT_DUR);
	}
}

void
gamePause()
{
	draw();
	// resume the game if needed
	if (arduboy.justPressed(LEFT_BUTTON))
	{
		gameTick = &gameMain;
	}
}

void
menuWin1()
{
	arduboy.setCursor(0, 0);
	arduboy.print(winText1);
	if (arduboy.justPressed(A_BUTTON))
	{
		gameTick = &gameSetup;
	}
}

void
menuWin2()
{
	arduboy.setCursor(0, 0);
	arduboy.print(winText2);
	if (arduboy.justPressed(A_BUTTON))
	{
		gameTick = &gameSetup;
	}
}
