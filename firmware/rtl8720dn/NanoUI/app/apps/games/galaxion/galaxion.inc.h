//
// Galaxian for Arduboy
//

#include "MyArduboy.h"
#include "game.h"

void setup()
{
  //mArduboy.begin();
  mArduboy.beginNoLogo();
  mArduboy.initRandomSeed();
  mArduboy.setFrameRate(60);

}

void loop()
{
  if (!(mArduboy.nextFrame())) {
    return;
  }

  mArduboy.clear();
  mGame.check_input();
  mGame.do_state();
  mGame.draw();
  mArduboy.display();
}

