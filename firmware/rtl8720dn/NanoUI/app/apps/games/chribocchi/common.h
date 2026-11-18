#pragma once

#include "MyArduboy.h"

/*  Defines  */

//#define DEBUG
#define APP_TITLE       "CHRI-BOCCHI CAT"
#define APP_CODE        "OBN-Y03"
#define APP_VERSION     "0.04"
#define APP_RELEASED    "DECEMBER 2017"

// The AVR EEPROM API keeps records away from the first 16 bytes. The shared
// engine's private RAM EEPROM is 1024 bytes and uses the same starting offset.
#define EEPROM_STORAGE_SPACE_START 16

/*  Typedefs  */

typedef unsigned char   uchar;
typedef unsigned int    uint;
typedef bool            boolean;

/*  Global Functions  */

void initLogo(void);
bool updateLogo(void);
void drawLogo(void);

void initTitle(void);
bool updateTitle(void);
void drawTitle(void);
uint8_t setLastScore(int score, uint32_t time);

void initGame(void);
bool updateGame(void);
void drawGame(void);

/*  Global Variables  */

extern MyArduboy arduboy;

/*  For Debugging  */

// BW16 apps must not touch the UART directly; diagnostics are compiled out.
#define dprint(...)
#define dprintln(...)
