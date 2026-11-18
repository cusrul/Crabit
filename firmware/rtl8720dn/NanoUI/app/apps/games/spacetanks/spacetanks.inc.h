// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "globals.h"
#include "state.h"

void setup()
{
    arduboy.beginNoLogo();
    arduboy.audio.begin();
    arduboy.setFrameRate(FRAMERATE);
}

void loop()
{
    if (!(arduboy.nextFrame())) return;

    arduboy.clear();
    mainGameLoop[gameState]();
    arduboy.display();
}
