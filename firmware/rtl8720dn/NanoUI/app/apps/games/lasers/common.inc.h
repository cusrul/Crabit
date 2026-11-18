// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    960
#define EEPROM_SIGNATURE    0x074E424FUL // "OBN\x07"

enum RECORD_STATE_T {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

/*  Global Variables  */

MyArduboy   arduboy;
RECORD_T    record;
bool        isRecordDirty;
uint16_t    lastScore;
bool        isInvalid;

/*  Local Functions  */

static uint16_t calcCheckSum();


/*  Local Variables  */

PROGMEM static const byte soundTick[] = {
    0x90, 69, 0, 10, 0x80, 0xF0
};

PROGMEM static const byte soundClick[] = {
    0x90, 74, 0, 20, 0x80, 0xF0
};

static RECORD_STATE_T   recordState = RECORD_NOT_READ;
static int16_t          eepAddr;
static bool             grayBlink = true;

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

static uint16_t calcCheckSum()
{
    uint16_t checkSum = (EEPROM_SIGNATURE & 0xFFFF) + (EEPROM_SIGNATURE >> 16) * 3;
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&record);
    for (int i = 0; i < sizeof(record) / 2; i++) {
        uint16_t word = p[0] | (uint16_t(p[1]) << 8);
        p += 2;
        checkSum += word * (i * 2 + 5);
    }
    return checkSum;
}

void clearRecord(void)
{
    eepSeek(EEPROM_ADDR_BASE);
    for (int i = 0; i < (sizeof(record) + 6) / 4; i++) {
        eepWrite32(0);
    }
    recordState = RECORD_INITIAL;
    dprintln(F("Clear EEPROM"));
}

bool enterScore(uint16_t score)
{
    int r = 10;
    uint16_t h;
    while (r > 0 && (h = record.hiscore[r - 1]) < score) {
        if (--r < 9) record.hiscore[r + 1] = h;
    }
    if (r < 10) record.hiscore[r] = score;
    lastScore = score;
    return (r == 0);
}

void drawFrame(int8_t x, int8_t y, int8_t w, int8_t h)
{
    arduboy.fillRect2(x + 1, y + 1, w, h, BLACK);
    arduboy.drawRect2(x, y, w, h, WHITE);
}

void drawNumber(int16_t x, int16_t y, int32_t value)
{
    arduboy.setCursor(x, y);
    arduboy.print(value);
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

void drawBitmapBW(int16_t x, int16_t y, const uint8_t *bitmap, uint8_t w, uint8_t h)
{
    arduboy.drawBitmap(x + 1, y + 1, bitmap, w, h, BLACK);
    arduboy.drawBitmap(x, y, bitmap, w, h, WHITE);
}

void clearScreenGray(void)
{
    uint8_t *sBuffer = arduboy.getBuffer();
    uint8_t b1 = (grayBlink) ? 0x55 : 0xaa;
    uint8_t b2 = ~b1;
    for (int i = 0; i < WIDTH * HEIGHT / 8; ++i) sBuffer[i] = (i & 1) ? b2 : b1;
    grayBlink = !grayBlink;
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
