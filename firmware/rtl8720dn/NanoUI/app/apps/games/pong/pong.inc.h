#include "game.hpp"

void
setup()
{
	arduboy.begin();
	arduboy.setFrameRate(30);
	arduboy.setTextSize(2);
	arduboy.clear();
}

void
loop()
{
	if (!arduboy.nextFrame())
	{
		return;
	}
	arduboy.pollButtons();
	tick();
	arduboy.display(CLEAR_BUFFER);
}
