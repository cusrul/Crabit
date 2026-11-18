// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

// Native/ARM pointers are wider than AVR program-memory words; use typed table
// access instead of pgm_read_word pointer casts.
#define callInitFunc(idx)   ((void) (moduleTable[idx].initFunc()))
#define callUpdateFunc(idx) (moduleTable[idx].updateFunc())
#define callDrawFunc(idx)   (moduleTable[idx].drawFunc())

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
    { initTitle,    updateTitle,    drawTitle },
    { initGame,     updateGame,     drawGame },
};

struct ModeState {
    MODE_T mode;
    bool dbgPrintEnabled = true;
    char dbgRecvChar = '\0';
};
static ModeState *g_mode = nullptr;
#define mode (g_mode->mode)
#define dbgPrintEnabled (g_mode->dbgPrintEnabled)
#define dbgRecvChar (g_mode->dbgRecvChar)

/*  For Debugging  */

#ifdef DEBUG

static void dbgCheckSerialRecv(void)
{
    int recv;
    while ((recv = Serial.read()) != -1) {
        switch (recv) {
        case 'd':
            dbgPrintEnabled = !dbgPrintEnabled;
            Serial.print("Debug output ");
            Serial.println(dbgPrintEnabled ? "ON" : "OFF");
            break;
        case 'r':
            clearRecord();
            break;
        }
        if (recv >= ' ' && recv <= '~') {
            dbgRecvChar = recv;
        }
    }
}
#endif

/*---------------------------------------------------------------------------*/

void setup()
{
#ifdef DEBUG
    Serial.begin(115200);
#endif
    arduboy.beginNoLogo();
    arduboy.setFrameRate(FPS);
    //arduboy.setTextColor(WHITE, WHITE);
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
    dbgRecvChar = '\0';
#endif
    uint8_t *buf = arduboy.getBuffer();
    if (isScreenInverted) {
        // The renderer keeps state between frames, so flip only for this
        // display and restore the buffer afterwards.
        for (int i = 0; i < WIDTH * HEIGHT / 8; i++) buf[i] ^= 0xFF;
    }
    arduboy.display();
    if (isScreenInverted) {
        for (int i = 0; i < WIDTH * HEIGHT / 8; i++) buf[i] ^= 0xFF;
    }
    if (mode != nextMode) {
        mode = nextMode;
        dprint(F("mode="));
        dprintln(mode);
        callInitFunc(mode);
    }
}
