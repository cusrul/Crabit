// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

// The AVR table stored 16-bit function pointers and fetched them through
// pgm_read_word. Native function pointers have no guaranteed 16-bit encoding,
// so keep a typed table; setup/loop otherwise follow the upstream sketch.
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
    { ::ArdubulletsSandbox::initLogo,  ::ArdubulletsSandbox::updateLogo,  ::ArdubulletsSandbox::drawLogo },
    { ::ArdubulletsSandbox::initTitle, ::ArdubulletsSandbox::updateTitle, ::ArdubulletsSandbox::drawTitle },
    { ::ArdubulletsSandbox::initGame,  ::ArdubulletsSandbox::updateGame,  ::ArdubulletsSandbox::drawGame },
};

struct ArdubulletsModeState {
    MODE_T mode;
};
static ArdubulletsModeState *g_mode = nullptr;
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
