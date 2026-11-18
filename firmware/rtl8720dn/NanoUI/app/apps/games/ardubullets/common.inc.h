#include <cstdint>
#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    1008
#define EEPROM_SIGNATURE    0x134E424FUL // "OBN\x13"

// Upstream stored its record at 1,012 followed by a 16-bit checksum: signature
// 1,008..1,011, record 1,012..1,021, checksum 1,022..1,023. avr-gcc does not
// pad the trailing uint16_t, so sizeof(RECORD_T) was 10 and the block I/O
// covered exactly that window. A 32-bit build aligns playCount to 4 and sizes
// the struct to 12, which would write two padding bytes and push the checksum
// past the 1,024-byte save window (silently discarded by RamEeprom). The wire
// image is therefore serialized field by field; the bytes below are the AVR
// record image, and eepWrite16/eepRead16 already use the same little-endian
// order for the signature.
constexpr size_t kRecordAddr = EEPROM_ADDR_BASE + 4;                    // 1,012
constexpr size_t kRecordWireBytes = 10;                                 // 1,012..1,021
constexpr size_t kRecordCheckSumAddr = kRecordAddr + kRecordWireBytes;  // 1,022..1,023
static_assert(kRecordWireBytes == 10, "upstream RECORD_T wire image is 10 bytes");
static_assert(kRecordCheckSumAddr + 2 == 1024, "checksum must end at the save edge");

#define PAD_REPEAT_DELAY    (FPS / 4)
#define PAD_REPEAT_INTERVAL (FPS / 12)

#define IMG_BUTTONS_W       7
#define IMG_BUTTONS_H       7

enum RECORD_STATE_T {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

/*  Port-owned state (on-demand heap, one live instance per port id)  */

struct ArdubulletsEngineState {
    MyArduboy2 arduboy;
};
struct ArdubulletsCommonState {
    RECORD_T    record;
    uint8_t     counter;
    int8_t      padX, padY, padRepeatCount;
    bool        isInvalid, isRecordDirty;
    RECORD_STATE_T recordState;
    int16_t     eepAddr;
};
static ArdubulletsEngineState *g_engine = nullptr;
static ArdubulletsCommonState *g_common = nullptr;
#define arduboy (g_engine->arduboy)
#define record (g_common->record)
#define counter (g_common->counter)
#define padX (g_common->padX)
#define padY (g_common->padY)
#define padRepeatCount (g_common->padRepeatCount)
#define isInvalid (g_common->isInvalid)
#define isRecordDirty (g_common->isRecordDirty)
#define recordState (g_common->recordState)
#define eepAddr (g_common->eepAddr)

/*  Local Functions  */

static uint16_t calcCheckSum(const uint8_t *wire);
static void encodeRecord(uint8_t *wire);
static void decodeRecord(const uint8_t *wire);

// AVR record image: the upstream bitfields occupied the lowest bits of the
// first little-endian 32-bit word (seed:24, rank:4, then four 1-bit flags).
// Extracting them explicitly keeps the wire bytes independent of this
// compiler's struct padding and bitfield allocation.
static void encodeRecord(uint8_t *wire)
{
    uint32_t word = (uint32_t(record.gameSeed) & 0x00FFFFFFu)
                  | (uint32_t(record.rank) & 0x0Fu) << 24
                  | (uint32_t(record.isCodeManual) & 0x01u) << 28
                  | (uint32_t(record.isBlinkLED) & 0x01u) << 29
                  | (uint32_t(record.isDrawEdge) & 0x01u) << 30
                  | (uint32_t(record.isCleared) & 0x01u) << 31;
    wire[0] = uint8_t(word);
    wire[1] = uint8_t(word >> 8);
    wire[2] = uint8_t(word >> 16);
    wire[3] = uint8_t(word >> 24);
    wire[4] = uint8_t(record.playFrames);
    wire[5] = uint8_t(record.playFrames >> 8);
    wire[6] = uint8_t(record.playFrames >> 16);
    wire[7] = uint8_t(record.playFrames >> 24);
    wire[8] = uint8_t(record.playCount);
    wire[9] = uint8_t(record.playCount >> 8);
}

static void decodeRecord(const uint8_t *wire)
{
    uint32_t word = uint32_t(wire[0]) | (uint32_t(wire[1]) << 8)
                  | (uint32_t(wire[2]) << 16) | (uint32_t(wire[3]) << 24);
    record.gameSeed = word & 0x00FFFFFFu;
    record.rank = (word >> 24) & 0x0Fu;
    record.isCodeManual = (word >> 28) & 0x01u;
    record.isBlinkLED = (word >> 29) & 0x01u;
    record.isDrawEdge = (word >> 30) & 0x01u;
    record.isCleared = (word >> 31) & 0x01u;
    record.playFrames = uint32_t(wire[4]) | (uint32_t(wire[5]) << 8)
                      | (uint32_t(wire[6]) << 16) | (uint32_t(wire[7]) << 24);
    record.playCount = uint16_t(wire[8]) | (uint16_t(wire[9]) << 8);
}

// eepSeek/eepRead*/eepWrite* are declared in common.h; this former TU-local
// block must not redeclare them static now that all bodies share one unit.

/*  Local Variables  */

PROGMEM static const uint8_t imgButtons[][7] = { // 7x7 x2
    { 0x3E, 0x47, 0x6B, 0x6D, 0x6D, 0x41, 0x3E },
    { 0x3E, 0x41, 0x55, 0x55, 0x51, 0x65, 0x3E }
};

/*---------------------------------------------------------------------------*/
/*                             Common Functions                              */
/*---------------------------------------------------------------------------*/

void readRecord(void)
{
    bool isVerified = false;
    uint8_t wire[kRecordWireBytes];
    eepSeek(EEPROM_ADDR_BASE);
    if (eepRead32() == EEPROM_SIGNATURE) {
        eepReadBlock(wire, sizeof(wire));
        if (eepRead16() == calcCheckSum(wire)) {
            // Only verified bytes reach the record, as upstream did: a torn or
            // foreign record is rejected and falls back to bounded defaults.
            decodeRecord(wire);
            isVerified = true;
        }
    }

    if (isVerified) {
        recordState = RECORD_STORED;
        isRecordDirty = false;
        dprintln(F("Read record from EEPROM"));
    } else {
        memset(&record, 0, sizeof(record));
        record.rank = GAME_RANK_DEFAULT;
        record.gameSeed = GAME_SEED_MAX;
        record.isBlinkLED = true;
        recordState = RECORD_INITIAL;
        isRecordDirty = true;
    }
    setSound(arduboy.isAudioEnabled()); // Load Sound ON/OFF
}

void writeRecord(void)
{
    if (!isRecordDirty) return;
    uint8_t wire[kRecordWireBytes];
    encodeRecord(wire);
    if (recordState == RECORD_INITIAL) {
        eepSeek(EEPROM_ADDR_BASE);
        eepWrite32(EEPROM_SIGNATURE);
    } else {
        eepSeek(kRecordAddr);
    }
    eepWriteBlock(wire, sizeof(wire));
    eepWrite16(calcCheckSum(wire));
    arduboy.saveAudioOnOff(); // RAM-only sound flag; no Flash persistence
    recordState = RECORD_STORED;
    isRecordDirty = false;
    dprintln(F("Write record to EEPROM"));
}

static uint16_t calcCheckSum(const uint8_t *wire)
{
    uint16_t checkSum = (EEPROM_SIGNATURE & 0xFFFF) + (EEPROM_SIGNATURE >> 16) * 3;
    // Same word factors as upstream, over the 10-byte AVR image rather than
    // this build's padded struct, so stored checksums match the original game.
    for (int i = 0; i < int(kRecordWireBytes / 2); i++) {
        uint16_t word = uint16_t(wire[i * 2]) | (uint16_t(wire[i * 2 + 1]) << 8);
        checkSum += word * (i * 2 + 5);
    }
    return checkSum;
}

void clearRecord(void)
{
    eepSeek(EEPROM_ADDR_BASE);
    // Signature + record + checksum: the whole 16-byte window at 1,008..1,023.
    for (int i = 0; i < int((kRecordWireBytes + 6) / 4); i++) {
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
void printGameSeed(int16_t x, int16_t y, uint32_t seed)
{
    arduboy.setCursor(x, y);
    for (int i = 0; i < GAME_SEED_TOKEN_MAX; i++) {
        int token = seed % GAME_SEED_TOKEN_VAL;
        char c = (token < GAME_SEED_TOKEN_ALP) ? 'A' + token : '.';
        arduboy.print(c);
        seed /= GAME_SEED_TOKEN_VAL;
    }
}

void drawButtonIcon(int16_t x, int16_t y, bool isB)
{
    arduboy.drawBitmap(x, y, imgButtons[isB],
            IMG_BUTTONS_W, IMG_BUTTONS_H, arduboy.getTextColor());
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
