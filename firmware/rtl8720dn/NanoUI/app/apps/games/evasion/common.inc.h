#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    640
#define EEPROM_SIGNATURE    0x144E424FUL // "OBN\x14"

#define PAD_REPEAT_DELAY    (FPS / 4)
#define PAD_REPEAT_INTERVAL (FPS / 12)

enum RECORD_STATE_T : uint8_t {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

#define IMG_INSTICON_W      16
#define IMG_SPEAKER_W       10
#define IMG_SOUNDONOFF_W    8
#define IMG_INST_H          16

/*  Global Variables  */

struct CommonState {
    MyArduboy2 arduboy;
    RECORD_T record;
    uint8_t counter;
    int8_t padX;
    int8_t padY;
    int8_t padRepeatCount;
    bool isInvalid;
    bool isRecordDirty;
    RECORD_STATE_T recordState = RECORD_NOT_READ;
    int16_t eepAddr;
    bool isInvalidInst;
};
static CommonState *g_common = nullptr;
#define arduboy (g_common->arduboy)
#define record (g_common->record)
#define counter (g_common->counter)
#define padX (g_common->padX)
#define padY (g_common->padY)
#define padRepeatCount (g_common->padRepeatCount)
#define isInvalid (g_common->isInvalid)
#define isRecordDirty (g_common->isRecordDirty)
#define recordState (g_common->recordState)
#define eepAddr (g_common->eepAddr)
#define isInvalidInst (g_common->isInvalidInst)

/*  Local Functions  */

static uint16_t calcCheckSum();

// eepSeek/eepRead*/eepWrite* are declared in common.h; once bodies share one
// translation unit they must not be redeclared static here.

/*  Local Variables  */

PROGMEM static const uint8_t imgInstIcon[][2][32] = { // 16x16 x2 x3
    {
        {
            0xE0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFC, 0xF8, 0xE0,
            0x07, 0x1F, 0x3F, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x3F, 0x1F, 0x07
        },{
            0x00, 0xE0, 0xF8, 0x3C, 0x1C, 0x0E, 0x46, 0x66, 0x66, 0x66, 0x66, 0x04, 0x04, 0xF8, 0xE0, 0x00,
            0x00, 0x07, 0x1F, 0x20, 0x20, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x7E, 0x20, 0x20, 0x1F, 0x07, 0x00
        }
    },{
        {
            0xE0, 0xF8, 0xFC, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFC, 0xF8, 0xE0,
            0x07, 0x1F, 0x3F, 0x7F, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F, 0x7F, 0x3F, 0x1F, 0x07
        },{
            0x00, 0xE0, 0xF8, 0x04, 0x04, 0x66, 0x66, 0x66, 0x66, 0x26, 0x06, 0x04, 0x44, 0xF8, 0xE0, 0x00,
            0x00, 0x07, 0x1F, 0x20, 0x20, 0x66, 0x66, 0x66, 0x66, 0x62, 0x70, 0x38, 0x3C, 0x1F, 0x07, 0x00
        }
    },{
        {
            0x30, 0x78, 0xFC, 0xFC, 0xF8, 0xFE, 0xFF, 0x7F, 0x7F, 0xFF, 0xFE, 0xF8, 0xFC, 0xFC, 0x78, 0x30,
            0x0C, 0x1E, 0x3F, 0x3F, 0x1F, 0x7F, 0xFF, 0xFE, 0xFE, 0xFF, 0x7F, 0x1F, 0x3F, 0x3F, 0x1E, 0x0C
        },{
            0x00, 0x30, 0x78, 0xF8, 0xF0, 0xF8, 0x7E, 0x3E, 0x3E, 0x7E, 0xF8, 0xF0, 0xF8, 0x78, 0x30, 0x00,
            0x00, 0x0C, 0x1E, 0x1F, 0x0F, 0x1F, 0x7E, 0x7C, 0x7C, 0x7E, 0x1F, 0x0F, 0x1F, 0x1E, 0x0C, 0x00
        }
    }
};

PROGMEM static const uint8_t imgSpeaker[2][20] = { // 10x16 x2
    {
        0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF8, 0xFC, 0xFE, 0xFE, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F,
        0x1F, 0x3F, 0x7F, 0x7F
    },{
        0x00, 0xE0, 0xE0, 0xE0, 0x00, 0xE0, 0xF0, 0xF8, 0xFC, 0x00, 0x00, 0x07, 0x07, 0x07, 0x00, 0x07,
        0x0F, 0x1F, 0x3F, 0x00
    }
};

PROGMEM static const uint8_t imgSoundOnOff[][2][16] = { // 8x16 x2 x2
    {
        { 0x00, 0x60, 0xE0, 0xC0, 0x80, 0xC0, 0xE0, 0x60, 0x00, 0x0C, 0x0E, 0x07, 0x03, 0x07, 0x0E, 0x0C },
        { 0x00, 0x00, 0x40, 0x80, 0x00, 0x80, 0x40, 0x00, 0x00, 0x00, 0x04, 0x02, 0x01, 0x02, 0x04, 0x00 }
    },{
        { 0xEC, 0xDC, 0xFB, 0xF7, 0xFE, 0xFC, 0xF8, 0xE0, 0x37, 0x3B, 0xDF, 0xEF, 0x7F, 0x3F, 0x1F, 0x07 },
        { 0x40, 0x88, 0x10, 0xE2, 0x04, 0x18, 0xE0, 0x00, 0x02, 0x11, 0x08, 0x47, 0x20, 0x18, 0x07, 0x00 }
    }
};


/*---------------------------------------------------------------------------*/
/*                             Common Functions                              */
/*---------------------------------------------------------------------------*/

void readRecord(void)
{
    bool isVerified = false;
    eepSeek(EEPROM_ADDR_BASE);
    if (eepRead32() == EEPROM_SIGNATURE) {
        eepReadBlock(&record, sizeof(record));
        isVerified = (eepRead16() == calcCheckSum());
    }

    if (isVerified) {
        recordState = RECORD_STORED;
        isRecordDirty = false;
        dprintln(F("Read record from EEPROM"));
    } else {
        memset(&record, 0, sizeof(record));
        record.speed = 3;
        record.acceleration = 1;
        record.density = 3;
        record.thickness = 3;
        recordState = RECORD_INITIAL;
        isRecordDirty = true;
    }
    setSound(arduboy.isAudioEnabled()); // Load Sound ON/OFF
}

void writeRecord(void)
{
    if (!isRecordDirty) return;
    if (recordState == RECORD_INITIAL) {
        eepSeek(EEPROM_ADDR_BASE);
        eepWrite32(EEPROM_SIGNATURE);
    } else {
        eepSeek(EEPROM_ADDR_BASE + 4);
    }
    eepWriteBlock(&record, sizeof(record));
    eepWrite16(calcCheckSum());
    arduboy.saveAudioOnOff(); // RAM-only sound flag; no Flash persistence
    recordState = RECORD_STORED;
    isRecordDirty = false;
    dprintln(F("Write record to EEPROM"));
}

static uint16_t calcCheckSum()
{
    uint16_t checkSum = (EEPROM_SIGNATURE & 0xFFFF) + (EEPROM_SIGNATURE >> 16) * 3;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&record);
    for (size_t i = 0; i < sizeof(record) / 2; i++) {
        uint16_t word = p[0] | (uint16_t(p[1]) << 8);
        p += 2;
        checkSum += word * (i * 2 + 5);
    }
    return checkSum;
}

void clearRecord(void)
{
    eepSeek(EEPROM_ADDR_BASE);
    for (size_t i = 0; i < (sizeof(record) + 6) / 4; i++) {
        eepWrite32(0);
    }
    recordState = RECORD_INITIAL;
    dprintln(F("Clear EEPROM"));
}

void handleDPad(void)
{
    padX = padY = 0;
    if (arduboy.buttonPressed(LEFT_BUTTON | RIGHT_BUTTON | UP_BUTTON | DOWN_BUTTON)) {
        if (++padRepeatCount >= (PAD_REPEAT_DELAY + PAD_REPEAT_INTERVAL)) {
            padRepeatCount = PAD_REPEAT_DELAY;
        }
        if (padRepeatCount == 1 || padRepeatCount == PAD_REPEAT_DELAY) {
            if (arduboy.buttonPressed(LEFT_BUTTON))  padX--;
            if (arduboy.buttonPressed(RIGHT_BUTTON)) padX++;
            if (arduboy.buttonPressed(UP_BUTTON))    padY--;
            if (arduboy.buttonPressed(DOWN_BUTTON))  padY++;
        }
    } else {
        padRepeatCount = 0;
    }
}

void drawTime(int16_t x, int16_t y, uint32_t frames)
{
    uint16_t h = frames / (FPS * 3600UL);
    uint8_t m = frames / (FPS * 60) % 60;
    uint8_t s = frames / FPS % 60;
    arduboy.setCursor(x, y);
    if (h == 0 && m == 0) {
        if (s < 10) arduboy.print('0');
        arduboy.print(s);
        arduboy.print('.');
        arduboy.print(frames / (FPS / 10) % 10);
    } else {
        if (h > 0) {
            arduboy.print(h);
            arduboy.print(':');
            if (m < 10) arduboy.print('0');
        }
        arduboy.print(m);
        arduboy.print(':');
        if (s < 10) arduboy.print('0');
        arduboy.print(s);
    }
}

void drawBitmapWithMask(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h)
{
    uint16_t size = w * ((h + 7) >> 3);
    arduboy.drawBitmap(x, y, bitmap, w, h, BLACK);
    arduboy.drawBitmap(x, y, bitmap + size, w, h);
}

void drawBitmapBordered(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h)
{
    for (int i = 0; i <= 4; i++) {
        int16_t dx, dy;
        if (i == 4) {
            dx = dy = 0;
        } else {
            dx = (i * 2 + 1) % 3 - 1;
            dy = (i * 2 + 1) / 3 - 1;
        }
        arduboy.drawBitmap(x + dx, y + dy, bitmap, w, h, (i == 4)); 
    }
}

SIMPLE_OP_T handleSimpleMode(void)
{
    SIMPLE_OP_T op = SIMPLE_OP_NONE;
    if (arduboy.buttonPressed(DOWN_BUTTON)) {
        if (counter < FPS + IMG_INST_H) {
            counter++;
            isInvalidInst = (counter > FPS);
        }
    } else {
        isInvalid = (counter > FPS);
        counter = 0;
    }
    if (counter > FPS) {
        if (arduboy.buttonDown(A_BUTTON)) {
            setSound(!arduboy.isAudioEnabled());
            playSoundClick();
            isInvalidInst = true;
            op = SIMPLE_OP_SOUND;
        } 
        if (arduboy.buttonDown(B_BUTTON)) op = SIMPLE_OP_SETTINGS;
    } else {
        if (arduboy.buttonDown(A_BUTTON | B_BUTTON)) op = SIMPLE_OP_START;
    }
    return op;
}

void drawSimpleModeInstruction(void)
{
    if (!isInvalid && isInvalidInst) {
        arduboy.fillRect(0, HEIGHT - IMG_INST_H, WIDTH, IMG_INST_H, BLACK);
    }
    if (isInvalid || isInvalidInst) {
        if (counter <= FPS) {
            arduboy.printEx(27, 58, F("PRESS   OR "));
            for (int i = 0; i < 2; i++) {
                int16_t x = 59 + i * 27;
                drawBitmapWithMask(x, 48, imgInstIcon[i][0], IMG_INSTICON_W, IMG_INST_H);
            }
        } else {
            int16_t y = HEIGHT - (counter - FPS);
            for (int i = 0; i < 3; i++) {
                int16_t x = (i == 2) ? 84 : 28 + i * 40;
                drawBitmapWithMask(x, y, imgInstIcon[i][0], IMG_INSTICON_W, IMG_INST_H);
            }
            drawBitmapWithMask(44, y, imgSpeaker[0], IMG_SPEAKER_W, IMG_INST_H);
            drawBitmapWithMask(54, y, imgSoundOnOff[arduboy.audio.enabled()][0],
                    IMG_SOUNDONOFF_W, IMG_INST_H);
        }
    }
    isInvalidInst = false;
}

/*---------------------------------------------------------------------------*/
/*                              Sound Functions                              */
/*---------------------------------------------------------------------------*/

void setSound(bool on)
{
    arduboy.setAudioEnabled(on);
    dprint(F("audioEnabled="));
    dprintln(on);
}

void playSoundTick(void)
{
    arduboy.playTone(440, 10);
}

void playSoundClick(void)
{
    arduboy.playTone(587, 20);
}

/*---------------------------------------------------------------------------*/
/*                             EEPROM Functions                              */
/*---------------------------------------------------------------------------*/

void eepSeek(int addr)
{
    eepAddr = (addr < 16) ? 16 : addr;
}

uint8_t eepRead8(void)
{
    return arduboy.eeprom.read(eepAddr++);
}

uint16_t eepRead16(void)
{
    uint16_t ret = eepRead8();
    ret |= uint16_t(eepRead8()) << 8;
    return ret;
}

uint32_t eepRead32(void)
{
    uint32_t ret = eepRead16();
    ret |= uint32_t(eepRead16()) << 16;
    return ret;
}

void eepReadBlock(void *p, size_t n)
{
    uint8_t *bytes = static_cast<uint8_t *>(p);
    while (n--) *bytes++ = eepRead8();
}

void eepWrite8(uint8_t val)
{
    arduboy.eeprom.update(eepAddr++, val);
}

void eepWrite16(uint16_t val)
{
    eepWrite8(val & 0xff);
    eepWrite8(val >> 8);
}

void eepWrite32(uint32_t val)
{
    eepWrite16(val & 0xffff);
    eepWrite16(val >> 16);
}

void eepWriteBlock(const void *p, size_t n)
{
    const uint8_t *bytes = static_cast<const uint8_t *>(p);
    while (n--) eepWrite8(*bytes++);
}
