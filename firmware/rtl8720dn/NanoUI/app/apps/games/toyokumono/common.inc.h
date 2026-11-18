#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    568
#define EEPROM_SIGNATURE    0x184E424FUL // "OBN\x18"

#define PAD_REPEAT_DELAY    (FPS / 4)
#define PAD_REPEAT_INTERVAL (FPS / 12)

enum RECORD_STATE_T : uint8_t {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

/*  Global Variables  */

struct CommonState {
    MyArduboy2 ab;
    RECORD_T record;
    FLYING_T dots[DOTS_MAX];
    uint16_t lastScore;
    uint8_t counter;
    uint8_t dotIndex;
    int8_t padX;
    int8_t padY;
    int8_t padRepeatCount;
    bool isInvalid;
    bool isRecordDirty;
    bool isTitleAnimation;
    bool isInstruction;
    RECORD_STATE_T recordState = RECORD_NOT_READ;
    int16_t eepAddr;
};
static CommonState *g_common = nullptr;
#define ab (g_common->ab)
#define record (g_common->record)
#define dots (g_common->dots)
#define lastScore (g_common->lastScore)
#define counter (g_common->counter)
#define dotIndex (g_common->dotIndex)
#define padX (g_common->padX)
#define padY (g_common->padY)
#define padRepeatCount (g_common->padRepeatCount)
#define isInvalid (g_common->isInvalid)
#define isRecordDirty (g_common->isRecordDirty)
#define isTitleAnimation (g_common->isTitleAnimation)
#define isInstruction (g_common->isInstruction)
#define recordState (g_common->recordState)
#define eepAddr (g_common->eepAddr)

/*  Local Functions  */

static uint16_t calcCheckSum();

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
    } else {
        memset(&record, 0, sizeof(record));
        recordState = RECORD_INITIAL;
        isRecordDirty = true;
    }
    setSound(ab.isAudioEnabled()); // Load Sound ON/OFF
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
    ab.audio.saveOnOff(); // Save Sound ON/OFF
    recordState = RECORD_STORED;
    isRecordDirty = false;
}

static uint16_t calcCheckSum()
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

void handleDPad(void)
{
    padX = padY = 0;
    if (ab.buttonPressed(LEFT_BUTTON | RIGHT_BUTTON | UP_BUTTON | DOWN_BUTTON)) {
        if (++padRepeatCount >= (PAD_REPEAT_DELAY + PAD_REPEAT_INTERVAL)) {
            padRepeatCount = PAD_REPEAT_DELAY;
        }
        if (padRepeatCount == 1 || padRepeatCount == PAD_REPEAT_DELAY) {
            if (ab.buttonPressed(LEFT_BUTTON))  padX--;
            if (ab.buttonPressed(RIGHT_BUTTON)) padX++;
            if (ab.buttonPressed(UP_BUTTON))    padY--;
            if (ab.buttonPressed(DOWN_BUTTON))  padY++;
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
    ab.setCursor(x, y);
    if (h == 0 && m == 0) {
        if (s < 10) ab.print('0');
        ab.print(s);
        ab.print('.');
        ab.print(frames / (FPS / 10) % 10);
    } else {
        if (h > 0) {
            ab.print(h);
            ab.print(':');
            if (m < 10) ab.print('0');
        }
        ab.print(m);
        ab.print(':');
        if (s < 10) ab.print('0');
        ab.print(s);
    }
}

void drawText(const char *p, int16_t y)
{
    while (pgm_read_byte(p) != '\e') {
        uint8_t len = strnlen(p, 21);
        ab.printEx(64 - len * 3, y, (const __FlashStringHelper *) p);
        p += len + 1;
        y += (len == 0) ? 2 : 6;
    }
}

void drawDots(void)
{
    for (FLYING_T *p = dots; p < &dots[DOTS_MAX]; p++) {
        if (p->y <= 0 || (p->y >> DECIMAL_BITS) >= HEIGHT) continue;
        ab.drawPixel(p->x >> DECIMAL_BITS, p->y >> DECIMAL_BITS, WHITE);
    }
}

/*---------------------------------------------------------------------------*/
/*                              Sound Functions                              */
/*---------------------------------------------------------------------------*/

void setSound(bool on)
{
    ab.setAudioEnabled(on);
}

void playSoundTick(void)
{
    ab.playTone(440, 10);
}

void playSoundClick(void)
{
    ab.playTone(587, 20);
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
    return ab.eeprom.read(eepAddr++);
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
    ab.eeprom.update(eepAddr++, val);
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
