#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    656
#define EEPROM_ADDR_MIGRATE 688
#define EEPROM_SIGNATURE    0x124E424FUL // "OBN\x12"

#define PAD_REPEAT_DELAY    (FPS / 4)
#define PAD_REPEAT_INTERVAL (FPS / 12)

enum RECORD_STATE_T {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

/*  Global Variables  */

struct CommonState {
    MyArduboy arduboy;
    RECORD_T record;
    int8_t padX;
    int8_t padY;
    int8_t padRepeatCount;
    uint32_t lastScore;
    bool isInvalid;
    bool isRecordDirty;
    bool isBMode;
    RECORD_STATE_T recordState = RECORD_NOT_READ;
    int16_t eepAddr;
};
static CommonState *g_common = nullptr;
#define arduboy (g_common->arduboy)
#define record (g_common->record)
#define padX (g_common->padX)
#define padY (g_common->padY)
#define padRepeatCount (g_common->padRepeatCount)
#define lastScore (g_common->lastScore)
#define isInvalid (g_common->isInvalid)
#define isRecordDirty (g_common->isRecordDirty)
#define isBMode (g_common->isBMode)
#define recordState (g_common->recordState)
#define eepAddr (g_common->eepAddr)

/*  Local Functions  */

static uint16_t calcCheckSum();

// eepSeek/eepRead*/eepWrite* are declared in common.h; this former TU-local
// block must not redeclare them static now that all bodies share one unit.

/*  Local Variables  */

PROGMEM static const byte soundTick[] = {
    0x90, 69, 0, 10, 0x80, 0xF0 // arduboy.tone2(440, 10);
};

PROGMEM static const byte soundClick[] = {
    0x90, 74, 0, 20, 0x80, 0xF0 // arduboy.tone2(587, 20);
};


/*---------------------------------------------------------------------------*/
/*                             Common Functions                              */
/*---------------------------------------------------------------------------*/

void readRecord(void)
{
    bool isVerified = false;
    bool isMigrated = false;
    for (int i = 0; !isVerified && i < 2; i++) {
        eepSeek((i == 0) ? EEPROM_ADDR_BASE : EEPROM_ADDR_MIGRATE);
        if (eepRead32() == EEPROM_SIGNATURE) {
            eepReadBlock(&record, sizeof(record));
            isVerified = (eepRead16() == calcCheckSum());
            isMigrated = (isVerified && i == 1);
        }
    }

    if (isVerified) {
        recordState = (isMigrated) ? RECORD_INITIAL : RECORD_STORED;
        isRecordDirty = isMigrated;
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

bool enterScore(uint32_t score, bool isB)
{
    int r;
    for (r = 3; r > 0; r--) {
        uint32_t hiscore = getHiscore(r - 1, isB);
        if (hiscore >= score) break;
        if (r <= 2) setHiscore(r, isB, hiscore);
    }
    if (r < 3) {
        setHiscore(r, isB, score);
    }
    lastScore = score;
    return (r == 0);
}

uint32_t getHiscore(uint8_t r, bool isB)
{
    uint8_t *p = record.hiscoreRaw + (isB * 3 + r) * 3;
    return (uint32_t)*p | (uint32_t)*(p + 1) << 8 | (uint32_t)*(p + 2) << 16;
}

void setHiscore(uint8_t r, bool isB, uint32_t score)
{
    uint8_t *p = record.hiscoreRaw + (isB * 3 + r) * 3;
    *p++ = score & 0xff;
    *p++ = score >> 8 & 0xff;
    *p++ = score >> 16 & 0xff;
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

void drawNumber(int16_t x, int16_t y, int32_t value)
{
    arduboy.setCursor(x, y);
    arduboy.print(value);
}

int8_t drawNumberR(int16_t x, int16_t y, int32_t value)
{
    int8_t ret = 0;
    do {
        arduboy.setCursor(x, y);
        arduboy.print((char)('0' + value % 10));
        value /= 10;
        x -= 6;
        ret++;
    } while (value > 0);
    return ret;
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
