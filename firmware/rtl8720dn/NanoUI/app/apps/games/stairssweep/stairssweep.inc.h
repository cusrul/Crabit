// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Typedefs  */

typedef struct {
    void(*initFunc)(void);
    MODE_T(*updateFunc)(void);
    void(*drawFunc)(void);
} MODULE_FUNCS;

/*  Local Constants  */

PROGMEM static const MODULE_FUNCS moduleTable[] = {
    { initLogo,     updateLogo,     drawLogo },
    { initTitle,    updateTitle,    drawTitle },
    { initGame,     updateGame,     drawGame },
};

/*  Local Functions (macros)  */

#define callInitFunc(idx)   (moduleTable[idx].initFunc())
#define callUpdateFunc(idx) (moduleTable[idx].updateFunc())
#define callDrawFunc(idx)   (moduleTable[idx].drawFunc())

/*  Local Variables  */

struct ModeState {
    MODE_T mode;
};
static ModeState *g_mode = nullptr;
#define mode (g_mode->mode)

/*---------------------------------------------------------------------------*/

void setup()
{
    arduboy.beginNoLogo();
    arduboy.setFrameRate(FPS);
    arduboy.setTextColor(WHITE, WHITE);
    mode = MODE_LOGO;
    callInitFunc(mode);
}

void loop()
{
    if (!(arduboy.nextFrame())) return;
    MODE_T nextMode = callUpdateFunc(mode);
    callDrawFunc(mode);
    arduboy.display();
    if (mode != nextMode) {
        mode = nextMode;
        callInitFunc(mode);
    }
}
