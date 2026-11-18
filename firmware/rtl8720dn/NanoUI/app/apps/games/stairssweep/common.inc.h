#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    736
#define EEPROM_SIGNATURE    0x094E424FUL // "OBN\x09"

enum RECORD_STATE_T {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

#define IMG_LABEL_W 5
#define IMG_LABEL_H 20

/*  Global Variables  */

struct CommonState {
    MyArduboyV arduboy;
    RECORD_T record;
    int8_t padX;
    int8_t padY;
    int8_t padRepeatCount;
    uint8_t startLevel;
    uint32_t lastScore;
    bool isInvalid;
    bool isRecordDirty;
    RECORD_STATE_T recordState = RECORD_NOT_READ;
    int16_t eepAddr;
};
static CommonState *g_common = nullptr;
#define arduboy (g_common->arduboy)
#define record (g_common->record)
#define padX (g_common->padX)
#define padY (g_common->padY)
#define padRepeatCount (g_common->padRepeatCount)
#define startLevel (g_common->startLevel)
#define lastScore (g_common->lastScore)
#define isInvalid (g_common->isInvalid)
#define isRecordDirty (g_common->isRecordDirty)
#define recordState (g_common->recordState)
#define eepAddr (g_common->eepAddr)

/*  Local Functions  */

static uint16_t calcCheckSum(void);
static void     printColonAnd2Digits(uint8_t value);

void     eepSeek(int addr);
uint8_t  eepRead8(void);
uint16_t eepRead16(void);
uint32_t eepRead32(void);
void     eepReadBlock(void *p, size_t n);
void     eepWrite8(uint8_t val);
void     eepWrite16(uint16_t val);
void     eepWrite32(uint32_t val);
void     eepWriteBlock(const void *p, size_t n);

/*  Local Variables  */

PROGMEM static const uint8_t imgLabelLevel[15] = { // 5x20
    0xE8, 0x88, 0xE8, 0x88, 0xEE, 0xEA, 0x8A, 0xEA, 0x8A, 0xE4, 0x08, 0x08, 0x08, 0x08, 0x0E
};

PROGMEM static const uint8_t imgLabelScore[15] = { // 5x20
    0xEE, 0xA8, 0xCE, 0xA8, 0xAE, 0x66, 0x8A, 0x8A, 0x8A, 0xEC, 0x06, 0x08, 0x0E, 0x02, 0x0C
};

PROGMEM static const byte soundTick[] = {
    0x90, 69, 0, 10, 0x80, 0xF0
};

PROGMEM static const byte soundClick[] = {
    0x90, 74, 0, 20, 0x80, 0xF0
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
    arduboy.audio.saveOnOff(); // Save Sound ON/OFF
    recordState = RECORD_STORED;
    isRecordDirty = false;
    dprintln(F("Write record to EEPROM"));
}

static uint16_t calcCheckSum(void)
{
    uint16_t checkSum = (EEPROM_SIGNATURE & 0xFFFF) + (EEPROM_SIGNATURE >> 16) * 3;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&record);
    for (int i = 0; i < int(sizeof(record) / 2); i++) {
        uint16_t word = p[i * 2] | (uint16_t(p[i * 2 + 1]) << 8);
        checkSum += word * (i * 2 + 5);
    }
    return checkSum;
}

void clearRecord(void)
{
    eepSeek(EEPROM_ADDR_BASE);
    for (int i = 0; i < int((sizeof(record) + 6) / 4); i++) {
        eepWrite32(0);
    }
    recordState = RECORD_INITIAL;
    dprintln(F("Clear EEPROM"));
}

bool enterScore(uint32_t score, uint8_t level)
{
    int r;
    for (r = 5; r > 0; r--) {
        HISCORE_T h = record.hiscore[r - 1];
        if (h.score >= score) break;
        if (r <= 4) record.hiscore[r] = h;
    }
    if (r < 5) {
        record.hiscore[r].score = score;
        record.hiscore[r].level = level;
    }
    lastScore = score;
    return (r == 0);
}

void handleDPadV(void)
{
    padX = padY = 0;
    if (arduboy.buttonPressed(LEFT_BUTTON_V | RIGHT_BUTTON_V | UP_BUTTON_V | DOWN_BUTTON_V)) {
        if (++padRepeatCount >= (PAD_REPEAT_DELAY + PAD_REPEAT_INTERVAL)) {
            padRepeatCount = PAD_REPEAT_DELAY;
        }
        if (padRepeatCount == 1 || padRepeatCount == PAD_REPEAT_DELAY) {
            if (arduboy.buttonPressed(LEFT_BUTTON_V))  padX--;
            if (arduboy.buttonPressed(RIGHT_BUTTON_V)) padX++;
            if (arduboy.buttonPressed(UP_BUTTON_V))    padY--;
            if (arduboy.buttonPressed(DOWN_BUTTON_V))  padY++;
        }
    } else {
        padRepeatCount = 0;
    }
    // B is reserved for rotate/cancel in game; do not also nudge with the
    // rotated movement axis while a B tap is active.
    if (arduboy.buttonPressed(B_BUTTON)) padX = 0;
}

void drawNumberV(int16_t x, int16_t y, int32_t value, ALIGN_T align)
{
    if (align != ALIGN_LEFT) {
        uint32_t tmp;
        if (value >= 0)  {
            tmp = value;
        } else {
            y += align * 3;
            tmp = -value;
        }
        do {
            y += align * 3;
            tmp /= 10;
        } while (tmp > 0);
    }
    arduboy.setCursor(x, y);
    arduboy.print(value);
}

void drawTime(int16_t x, int16_t y, uint32_t frames, ALIGN_T align)
{
    if (align == ALIGN_RIGHT) y += 36;
    drawNumberV(x, y, frames / (FPS * 3600UL), align);
    printColonAnd2Digits(frames / (FPS * 60) % 60);
    printColonAnd2Digits(frames / FPS % 60);
}

static void printColonAnd2Digits(uint8_t value)
{
    arduboy.print(':');
    if (value < 10) arduboy.print('0');
    arduboy.print(value);
}

void drawLabelLevel(int16_t x, int16_t y)
{
    arduboy.drawBitmap(x, y, imgLabelLevel, IMG_LABEL_W, IMG_LABEL_H, WHITE);
}

void drawLabelScore(int16_t x, int16_t y)
{
    arduboy.drawBitmap(x, y, imgLabelScore, IMG_LABEL_W, IMG_LABEL_H, WHITE);
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
    arduboy.playScore2(soundTick, 255);
}

void playSoundClick(void)
{
    arduboy.playScore2(soundClick, 255);
}

/*---------------------------------------------------------------------------*/
/*                             EEPROM Functions                              */
/*---------------------------------------------------------------------------*/

void eepSeek(int addr)
{
    eepAddr = max(addr, 16);
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
    uint8_t *dst = static_cast<uint8_t *>(p);
    while (n--) *dst++ = eepRead8();
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
    const uint8_t *src = static_cast<const uint8_t *>(p);
    while (n--) eepWrite8(*src++);
}
