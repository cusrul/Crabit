// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

// Typed module table: AVR/Realtek 16-bit function-pointer reads are invalid.
typedef void (*MODULE_INIT_FUNC)(void);
typedef MODE_T (*MODULE_UPDATE_FUNC)(void);
typedef void (*MODULE_DRAW_FUNC)(void);

typedef struct
{
    MODULE_INIT_FUNC    initFunc;
    MODULE_UPDATE_FUNC  updateFunc;
    MODULE_DRAW_FUNC    drawFunc;
} MODULE_FUNCS;

/*  Local Variables  */

static const MODULE_FUNCS moduleTable[] = {
    { initLogo,  updateLogo,  drawLogo },
    { initTitle, updateTitle, drawTitle },
    { initGame,  updateGame,  drawGame },
};

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
    mode = MODE_LOGO;
    moduleTable[mode].initFunc();
}

void loop()
{
    if (!arduboy.nextFrame()) return;
    MODE_T nextMode = moduleTable[mode].updateFunc();
    moduleTable[mode].drawFunc();
    arduboy.display();
    if (mode != nextMode) {
        mode = nextMode;
        moduleTable[mode].initFunc();
    }
}
