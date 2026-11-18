// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

// Native/ARM pointers are wider than AVR's 16-bit program-memory words, so the
// dispatch uses typed table access instead of pgm_read_word pointer casts.
#define callInitFunc(idx)   ((void) (moduleTable[idx].initFunc()))
#define callUpdateFunc(idx) (moduleTable[idx].updateFunc())
#define callDrawFunc(idx)   (moduleTable[idx].drawFunc())

/*  Typedefs  */

typedef struct {
    void    (*initFunc)(void);
    MODE_T  (*updateFunc)(void);
    void    (*drawFunc)(void);
} MODULE_FUNCS;

/*  Local Variables  */

PROGMEM static const MODULE_FUNCS moduleTable[] = {
    { initLogo,     updateLogo,     drawLogo    },
    { initMenu,     updateMenu,     drawMenu    },
    { initPuzzle,   updatePuzzle,   drawPuzzle  },
    { initGallery,  updateGallery,  drawGallery },
};

struct ModeState {
    MODE_T mode;
    bool dbgPrintEnabled = true;
    char dbgRecvChar = '\0';
    uint8_t dbgCaptureMode = 0;
};
static ModeState *g_mode = nullptr;
#define mode (g_mode->mode)
#define dbgPrintEnabled (g_mode->dbgPrintEnabled)
#define dbgRecvChar (g_mode->dbgRecvChar)
#define dbgCaptureMode (g_mode->dbgCaptureMode)

/*  For Debugging  */

#ifdef DEBUG

static void dbgCheckSerialRecv(void) {
    int recv;
    while ((recv = Serial.read()) != -1) {
        switch (recv) {
        case 'd':
            dbgPrintEnabled = !dbgPrintEnabled;
            Serial.print("Debug output ");
            Serial.println(dbgPrintEnabled ? "ON" : "OFF");
            break;
        case 'c':
            dbgCaptureMode = 1;
            break;
        case 'C':
            dbgCaptureMode = 2;
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

static void dbgScreenCapture() {
    if (dbgCaptureMode) {
        Serial.write((const uint8_t *)arduboy.getBuffer(), WIDTH * HEIGHT / 8);
        if (dbgCaptureMode == 1) {
            dbgCaptureMode = 0;
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
    arduboy.setFrameRate(60);
    mode = MODE_LOGO;
    callInitFunc(mode);
}

void loop()
{
#ifdef DEBUG
    dbgCheckSerialRecv();
#endif
    if (!(arduboy.nextFrame())) {
        return;
    }
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
