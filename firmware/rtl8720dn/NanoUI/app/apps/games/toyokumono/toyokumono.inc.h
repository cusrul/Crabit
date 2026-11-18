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
    ab.beginNoLogo();
    ab.setFrameRate(FPS);
    //ab.setTextColors(WHITE, WHITE);
    ab.initAudio(1);
    readRecord();
    isTitleAnimation = true;
    callInitFunc(mode);
}

void loop()
{
    if (!(ab.nextFrame())) return;
    handleDPad();
    MODE_T nextMode = callUpdateFunc(mode);
    callDrawFunc(mode);
    ab.display();
    if (mode != nextMode) {
        mode = nextMode;
        callInitFunc(mode);
    }
}
