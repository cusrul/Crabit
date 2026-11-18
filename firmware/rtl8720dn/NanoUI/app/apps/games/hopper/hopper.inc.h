#include "common.h"

// Upstream hopper.ino. The Arduboy sketch's module table becomes typed native
// function pointers; the function pointers are never read through AVR's
// 16-bit pgm_read_word, which would truncate them on the Realtek target.

/*  Defines  */

// MODE and HopperModeState live in port.h so the mode is part of the typed
// on-demand State.

/*  Typedefs  */

typedef struct {
    void (*initFunc)(void);
    bool (*updateFunc)(void);
    void (*drawFunc)(void);
} MODULE_FUNCS;

/*  Local Variables  */

static const MODULE_FUNCS moduleTable[] = {
    { initLogo,  updateLogo,  drawLogo },
    { initTitle, updateTitle, drawTitle },
    { initGame,  updateGame,  drawGame },
};

/*---------------------------------------------------------------------------*/

void setup()
{
    arduboy.beginNoLogo();
    arduboy.setFrameRate(60);
    mode = LOGO_MODE;
    moduleTable[LOGO_MODE].initFunc();
}

void loop()
{
    if (!(arduboy.nextFrame())) return;
    bool isDone = moduleTable[mode].updateFunc();
    moduleTable[mode].drawFunc();
    arduboy.display();
    if (isDone) {
        mode = (mode == TITLE_MODE) ? GAME_MODE : TITLE_MODE;
        moduleTable[mode].initFunc();
        dprint("mode=");
        dprintln(mode);
    }
}
