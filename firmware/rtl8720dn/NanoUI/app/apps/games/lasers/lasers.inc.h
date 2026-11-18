// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

#define callInitFunc(idx)   moduleTable[idx].initFunc()
#define callUpdateFunc(idx) moduleTable[idx].updateFunc()
#define callDrawFunc(idx)   moduleTable[idx].drawFunc()

/*  Typedefs  */

typedef struct
{
    void(*initFunc)(void);
    MODE_T(*updateFunc)(void);
    void(*drawFunc)(void);
} MODULE_FUNCS;

/*  Local Variables  */

PROGMEM static const MODULE_FUNCS moduleTable[] = {
    { initLogo,     updateLogo,     drawLogo },
    { initWarn,     updateWarn,     drawWarn },
    { initTitle,    updateTitle,    drawTitle },
    { initGame,     updateGame,     drawGame },
};

static MODE_T mode;

/*  For Debugging  */


/*---------------------------------------------------------------------------*/

void setup()
{
    resetPortState();
    arduboy.beginNoLogo();
    arduboy.setFrameRate(FPS);
    arduboy.setTextColor(WHITE, WHITE);
    mode = MODE_LOGO;
    callInitFunc(mode);
}

void loop()
{
#ifdef DEBUG
    dbgCheckSerialRecv();
#endif
    if (!(arduboy.nextFrame())) return;
    MODE_T nextMode = callUpdateFunc(mode);
    callDrawFunc(mode);
#ifdef DEBUG
    dbgScreenCapture();
    dbgRecvChar = '\0';
#endif
    arduboy.display();
    if (mode != nextMode) {
        mode = nextMode;
        dprint(F("mode="));
        dprintln(mode);
        callInitFunc(mode);
    }
}
