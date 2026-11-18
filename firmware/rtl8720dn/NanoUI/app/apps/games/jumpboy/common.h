#pragma once

// Shared declarations for the original JumpBoy modules. The .ino bodies are
// included privately under JumpBoySandbox by port.h.

#include "MyArduboy.h"
#include "Bitmaps.h"

// The Arduino AVR core's `boolean`; the host runtime only has bool.
typedef bool boolean;

/*  Original note frequencies (silent on this build)  */

#define NOTE_A1  110  // spring
#define NOTE_C2  131  // miss spring item(-)
#define NOTE_G2  196
#define NOTE_C3  262  // add coins item
#define NOTE_G3  392  // coin
#define NOTE_C4  523  // heart item, spring item(+), flag item

/*  Game modes  */

#define MODE_TITLE    1
#define MODE_START    2
#define MODE_RUNNING  3
#define MODE_GAMECLEAR 4
#define MODE_GAMEOVER 5

using ArduboyTones = ArduboyTonesCompat;

/*  Globals shared by the original files  */

extern JumpBoyArduboy arduboy;
extern ArduboyTones   sound;

extern uint8_t pMode;

extern int pX;
extern int pY;

extern uint8_t lives;
extern uint8_t level;

/*  Cross-file entry points  */

void levelStart(int lvl);
void finishLevelStart(void);
void updateStart(void);

void stageClear(void);
void updateStageClear(void);

void miss(void);
void updateGameOver(void);

void drawHeader(void);
void drawPlayer(void);
void displayTitle(void);

void initCoin(int concurrent_coin_max, bool is_puzzle);
uint8_t getCoinQty(void);
void moveCoin(void);
void drawCoin(void);

void initPowerUp(void);
void movePowerUp(void);
void drawPowerUp(void);

void initSpring(void);
void drawSpring(void);
void moveSpring(void);
void collisionSpring(bool &is_miss);
void setAddSpring(void);
void setDelSpring(void);
void setAddMaxSpring(void);
void initSpeedScroll(void);
void speedupScroll(void);

// Re-arms every transient global on app entry/unmount. The session record is
// RAM-only and deliberately cleared with the rest of the run.
void resetGameState(void);
