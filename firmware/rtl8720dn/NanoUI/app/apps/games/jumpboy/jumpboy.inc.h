// Include-only body of the original JumpBoy sketch. The blocking title loop
// and delay() waits are expressed as frame states; the game rules, bitmaps,
// springs, coins, power-ups and scoring are unchanged.
#include "common.h"

/*----------------------------------
               JumpBoy

    Created by:takaiwa@gmail.com
    Game Copyright (C) 2016 takaiwa@gmail.com

  ----------------------------------*/


// The original logic was gated to 30 Hz by millis(); the adapter runs at
// 60 Hz, so every second accepted frame is one original tick.
const uint8_t LOGIC_EVERY = 2;

// Frame counters replacing the original delay(2000)/delay(3000) calls.







const uint8_t MAXSPD = 4;
const uint8_t MAXLIVES = 12;





/*-------------------------------
    levelStart
  ------------------------------*/
void levelStart(int lvl)
{
  stage = 1;
  pASpeed = 2;
  savepASpeed = 2;
  pWaitFlg = false;
  missHold = 0;

  arduboy.clear();

  if (lvl == 1) {
    stage = 1;
    lives = 3;
    initSpeedScroll();
    arduboy.drawSlowXYBitmap(17, 10, bStart, 96, 48, 1);
  }

  arduboy.display();

  // Original delay(2000) at 30 Hz.
  pendingLevel = lvl;
  startWait = 60;
}

/*-------------------------------
    finishLevelStart
  ------------------------------*/
void finishLevelStart()
{
  initSpring();

  initCoin(coinMax, false);

  initPowerUp();

  level = pendingLevel;
  pY = 24;

  pMode = MODE_RUNNING;
}

void updateStart()
{
  if (startWait > 0) {
    --startWait;
    return;
  }
  finishLevelStart();
}

/*-------------------------------
    miss
  ------------------------------*/
void miss()
{
  pSpeed = 0;
  sound.tone(NOTE_C2, 160);
  initPowerUp();
  lives--;
  if (lives <= 0) {
    // Original delay(3000) before returning to the title.
    overWait = 90;
    pMode = MODE_GAMEOVER;
  }
}

void updateGameOver()
{
  if (overWait > 0) {
    --overWait;
    return;
  }
  pMode = MODE_TITLE;
  titleFlash = 0;
}

/*-------------------------------
    stageClear
  ------------------------------*/
void stageClear()
{
  record = stage;

  pASpeed += 0.5f;
  speedupScroll();

  if (pASpeed > 4) {
    pASpeed = 2;
  }

  savepASpeed = pASpeed;
  stage++;

  initSpring();
  initCoin(coinMax, false);

  arduboy.clear();
  arduboy.drawSlowXYBitmap(17, 10, bClear, 96, 48, 1);
  arduboy.display();

  // Original delay(2000) at 30 Hz; the clear screen stays up.
  clearWait = 60;
  pMode = MODE_GAMECLEAR;
}

void updateStageClear()
{
  if (clearWait > 0) {
    --clearWait;
    return;
  }
  pMode = MODE_RUNNING;
}

/*--------------------------
     Draw Header
  -------------------------*/
void drawHeader()
{
  arduboy.fillRect(0, 0, 127, 10, 1);
  for (int i = 0; i < lives; i++) {
    arduboy.drawBitmap(3 + (i * 10), 1, bHeart, 8, 8, 0);
  }
  arduboy.setCursor(72, 1);
  arduboy.write(' ');
  arduboy.write('C');
  arduboy.write('O');
  arduboy.write('I');
  arduboy.write('N');
  arduboy.write(' ');
  arduboy.print(getCoinQty());
  arduboy.write(' ');
}

/*-------------------------
    Draw Player
  ------------------------ */
void drawPlayer()
{
  bool is_miss = false;
  if (pY >= 46) {
    collisionSpring(is_miss);

    if (!is_miss) {
      pDir = 270;
      pYY = -1 * pASpeed;
    } else {
      pY = 12;

      // check gameover
      miss();

      savepASpeed = pASpeed;
      pASpeed = 0;
      pWaitFlg = true;
      // The original held the last frame for three seconds after every miss.
      missHold = (pMode == MODE_RUNNING) ? 90 : 0;
      return;
    }
  }
  if (pY <= 12) {
    pDir == 90; // Original no-op comparison, kept as written upstream.
    pYY = 1 * pASpeed;
  }
  pY += pYY;

  arduboy.drawSlowXYBitmap(pX, pY, bMan, 8, 8, 1);
}

/*-------------------------
    displayTitle
  ------------------------ */
void displayTitle()
{
  arduboy.clear();

  arduboy.setCursor(36, 1);
  arduboy.print("JUMP BOY");

  arduboy.drawSlowXYBitmap(14, 12, bTitle, 104, 36, 1);

  titleFlash++;
  titleFlash %= 50;

  if (record > 0) {
    arduboy.setCursor(30, 44);
    arduboy.print(" record: ");
    arduboy.print(record);
    arduboy.print(" ");
  }

  if (titleFlash < 25) {
    arduboy.setCursor(30, 54);
    arduboy.print("Press A or B");
  }

  arduboy.display();

  if (arduboy.pressed(A_BUTTON) || arduboy.pressed(B_BUTTON)) {
    levelStart(1);
    pMode = MODE_START;
  }
}

/*-------------------------
    setup
  ------------------------ */
void setup()
{
  arduboy.begin();
  arduboy.audio.on();

  logicDivider = 0;
  titleFlash = 0;
  // Original setup() entered the blocking title loop here.
  pMode = MODE_TITLE;

  arduboy.initRandomSeed();
}

/*-------------------------
    LOOP
  ------------------------ */
void loop()
{
  if (!arduboy.nextFrame()) return;

  if (++logicDivider < LOGIC_EVERY) return;
  logicDivider = 0;

  if (pMode == MODE_TITLE) {
    displayTitle();
    return;
  }

  if (pMode == MODE_START) {
    updateStart();
    return;
  }

  if (pMode == MODE_GAMECLEAR) {
    updateStageClear();
    return;
  }

  if (pMode == MODE_GAMEOVER) {
    updateGameOver();
    return;
  }

  if (pWaitFlg) {
    if (missHold > 0) {
      --missHold;
      return;
    }
    pWaitFlg = false;
    return;
  }

  if (pSpeed == 0) {
    frame = 0;
  }

  prevPX = pX;
  prevPY = pY;
  pSpeed = 0;

  if (pASpeed == 0) {
    if (arduboy.buttonsState()) {
      pASpeed = 1;
      pSpeed = pASpeed;
    }
  } else {
    if (arduboy.pressed(UP_BUTTON)) {
      pDir = 90;
      pASpeed = savepASpeed;
      pSpeed = pASpeed;
    } else if (arduboy.pressed(DOWN_BUTTON)) {
      pDir = 270;
      pASpeed = savepASpeed;
      pSpeed = pASpeed;
    } else if (arduboy.pressed(LEFT_BUTTON)) {
      pDir = 180;
      pASpeed = savepASpeed;
      pSpeed = pASpeed;
    } else if (arduboy.pressed(RIGHT_BUTTON)) {
      pDir = 0;
      pASpeed = savepASpeed;
      pSpeed = pASpeed;
    }
  }

  if (pSpeed > 0) {
    switch (pDir) {
      case 180:
        pX -= pSpeed;
        break;
      case 0:
        pX += pSpeed;
        break;
    }
  }

  if (pY > 56 - boundH) {
    pY = 56 - boundH;
  }

  if (pY < boundY) {
    pY = boundY;
  }

  if (pX > 120) {
    pX = 120;
  }

  if (pX < 8) {
    pX = 8;
  }

  if (prevPX != pX || prevPY != pY) {
    frame += animSpeed * pASpeed;
  }
  if (frame > 4) {
    frame = 0;
  }

  movePowerUp();
  moveCoin();
  moveSpring();

  // stageClear()/miss() may have changed the mode above.
  if (pMode != MODE_RUNNING) return;

  arduboy.clear();

  drawHeader();
  drawSpring();

  drawPlayer();
  drawPowerUp();
  drawCoin();

  arduboy.display();
}
