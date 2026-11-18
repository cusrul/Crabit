#ifndef HOPPER_COMMON_H
#define HOPPER_COMMON_H

// Common declarations for the actual OBONO Hopper sources. The upstream
// translation units are kept as include-only `.inc.h` bodies; this header is
// included once inside HopperSandbox by port.h.

#include "MyArduboy2.h"

/*  Defines  */

//#define DEBUG
#define APP_TITLE       "HOPPER"
#define APP_CODE        "OBN-Y02"
#define APP_VERSION     "0.22"
#define APP_RELEASED    "NOVEMBER 2017"

// Upstream game.cpp constants. They live here because the typed on-demand
// State needs the fixed-size FLOOR/CHIP shapes before the bodies are included.
#define FLOORS_MANAGE   10
#define FLOORS_LEVEL    50
#define FLOOR_Z_MAX     4096
#define CHIPS           64
#define CHIP_ZRANGE     1024
#define CENTER_X        64
#define CENTER_Y        32
#define PI              3.141592653589793

// AVR's EEPROM library reserves the first 16 bytes; Hopper's own record starts
// at 800, so the clamp is only kept for source parity.
#define EEPROM_STORAGE_SPACE_START 16

/*  Typedefs  */

typedef unsigned char   uchar;
typedef unsigned int    uint;

typedef struct {
    uint8_t type;
    uint8_t pos;
    int16_t x, y, z;
    int16_t vx, vy;
    uint8_t size, sizeBack;
} FLOOR;

typedef struct
{
    int16_t x, y;
} CHIP;

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

/*  For Debugging  */

#ifdef DEBUG
extern bool             dbgPrintEnabled;
extern char             dbgRecvChar;
#define dprint(...)     (!dbgPrintEnabled || Serial.print(__VA_ARGS__))
#define dprintln(...)   (!dbgPrintEnabled || Serial.println(__VA_ARGS__))
#else
#define dprint(...)
#define dprintln(...)
#endif

#endif // HOPPER_COMMON_H
