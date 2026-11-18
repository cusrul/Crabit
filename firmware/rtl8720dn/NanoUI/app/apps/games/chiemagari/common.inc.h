// Include-only body; .h suffix preserves Arduino staging without auto-compilation.
#include "common.h"

/*  Defines  */

#define EEPROM_ADDR_BASE    864
#define EEPROM_SIGNATURE    0x044E424FUL // "OBN\x04"
#define EEPROM_INIT_CHECKSUM ((EEPROM_SIGNATURE & 0xFFFF) + (EEPROM_SIGNATURE >> 16) * 3)

#define EEPROM_ADDR_PIECES  (EEPROM_ADDR_BASE + 4)
#define EEPROM_SIZE_PIECES  (PIECES * 2)
#define EEPROM_ADDR_CONFIGS (EEPROM_ADDR_PIECES + EEPROM_SIZE_PIECES)

#define EEPROM_ADDR_PATTERN 32
#define EEPROM_SIZE_PATTERN PIECES
// Largest pattern count whose 10-byte slots plus trailing checksum stay below
// the record block at EEPROM_ADDR_BASE. Upstream has no guard; filling it would
// otherwise let the pattern area overwrite the record.
#define MAX_PATTERN_COUNT   ((EEPROM_ADDR_BASE - EEPROM_ADDR_PATTERN - 2) / EEPROM_SIZE_PATTERN)

#define PAD_REPEAT_DELAY    15
#define PAD_REPEAT_INTERVAL 5

enum RECORD_T {
    RECORD_NOT_READ = 0,
    RECORD_INITIAL,
    RECORD_STORED,
};

/*  Global Variables  */

struct CommonState {
    MyArduboy arduboy;
    RECORD_T recordState = RECORD_NOT_READ;
    bool isDirty = false;
    PIECE_T pieceAry[PIECES];
    bool isHelpVisible;
    uint8_t clearCount;
    uint32_t playFrames;
    int8_t padX;
    int8_t padY;
    int8_t padRepeatCount;
    int8_t helpX;
    int8_t helpY;
    int16_t eepAddr;
    uint16_t encPiecesCheckSum;
};
static CommonState *g_common = nullptr;
#define arduboy (g_common->arduboy)
#define recordState (g_common->recordState)
#define isDirty (g_common->isDirty)
#define pieceAry (g_common->pieceAry)
#define isHelpVisible (g_common->isHelpVisible)
#define clearCount (g_common->clearCount)
#define playFrames (g_common->playFrames)
#define padX (g_common->padX)
#define padY (g_common->padY)
#define padRepeatCount (g_common->padRepeatCount)
#define helpX (g_common->helpX)
#define helpY (g_common->helpY)
#define eepAddr (g_common->eepAddr)
#define encPiecesCheckSum (g_common->encPiecesCheckSum)

/*  Local Variables  */

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
        uint16_t checkSum = EEPROM_INIT_CHECKSUM;
        for (int i = 0; i < 13; i++) {
            checkSum += eepRead16() * (i * 2 + 5);
        }
        isVerified = (eepRead16() == checkSum);
    }

    if (isVerified) {
        readPieces();
        //eepSeek(EEPROM_ADDR_CONFIGS);
        isHelpVisible = eepRead8();
        clearCount = eepRead8();
        playFrames = eepRead32();
        recordState = RECORD_STORED;
        dprintln(F("Read record from EEPROM"));
        dprint(F("clearCount="));
        dprintln(clearCount);
        dprint(F("playFrames="));
        dprintln(playFrames);
        verifyEncodedPieces();
    } else {
        resetPieces();
        isHelpVisible = true;
        clearCount = 0;
        playFrames = 0;
        recordState = RECORD_INITIAL;
        isDirty = true;
    }
    setGalleryIndex(clearCount - 1);
    setSound(arduboy.audio.enabled()); // Load Sound ON/OFF
}

void readPieces(void)
{
    eepSeek(EEPROM_ADDR_PIECES);
    eepReadBlock(pieceAry, EEPROM_SIZE_PIECES);
    dprintln(F("Read pieces from EEPROM"));
}

void readEncodedPieces(uint8_t idx, CODE_T *pCode)
{
    eepSeek(EEPROM_ADDR_PATTERN + idx * EEPROM_SIZE_PATTERN);
    eepReadBlock(pCode, EEPROM_SIZE_PATTERN);
    dprint(F("Read encoded pieces from EEPROM: idx="));
    dprintln(idx);
}

void verifyEncodedPieces(void)
{
    encPiecesCheckSum = 0;
    if (clearCount == 0) {
        return;
    }
    eepSeek(EEPROM_ADDR_PATTERN);
    for (int i = 0; i < clearCount * (EEPROM_SIZE_PATTERN / 2); i++) {
        encPiecesCheckSum += eepRead16() * (i * 2 + 1);
    }
    if (eepRead16() != encPiecesCheckSum) {
        clearCount = 0;
        encPiecesCheckSum = 0;
        isDirty = true;
        dprintln(F("Encoded pieces verification failed!"));
    }
}

void writeRecord(void)
{
    if (recordState == RECORD_INITIAL) {
        eepSeek(EEPROM_ADDR_BASE);
        eepWrite32(EEPROM_SIGNATURE);
    } else {
        eepSeek(EEPROM_ADDR_PIECES);
    }
    eepWriteBlock(pieceAry, EEPROM_SIZE_PIECES);
    eepWrite8(isHelpVisible);
    eepWrite8(clearCount);
    eepWrite32(playFrames);

    uint16_t checkSum = EEPROM_INIT_CHECKSUM;
    for (int i = 0; i < PIECES; i++) {
        uint16_t word;
        memcpy(&word, (const uint8_t *) pieceAry + i * sizeof(PIECE_T), sizeof(word));
        checkSum += word * (i * 2 + 5);
    }
    checkSum += (isHelpVisible | clearCount << 8) * 25;
    checkSum += (playFrames & 0xFFFF) * 27 + (playFrames >> 16) * 29;
    eepWrite16(checkSum);

    arduboy.audio.saveOnOff(); // Save Sound ON/OFF
    recordState = RECORD_STORED;
    isDirty = false;
    dprintln(F("Write record to EEPROM"));
}

void writeEncodedPieces(uint8_t idx, CODE_T *pCode)
{
    eepSeek(EEPROM_ADDR_PATTERN + idx * EEPROM_SIZE_PATTERN);
    eepWriteBlock(pCode, EEPROM_SIZE_PATTERN);
    for (int i = 0; i < (EEPROM_SIZE_PATTERN / 2); i++) {
        uint16_t word;
        memcpy(&word, (const uint8_t *) pCode + i * 2, sizeof(word));
        encPiecesCheckSum += word * (idx * EEPROM_SIZE_PATTERN + i * 2 + 1);
    }
    eepWrite16(encPiecesCheckSum); // Update check-sum
    dprint(F("Write encoded pieces to EEPROM: idx="));
    dprintln(idx);
}

void clearRecord(void)
{
    eepSeek(EEPROM_ADDR_BASE);
    for (int i = 0; i < 8; i++) {
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

/*---------------------------------------------------------------------------*/
/*                              Sound Functions                              */
/*---------------------------------------------------------------------------*/

void setSound(bool on)
{
    if (on) {
        arduboy.audio.on();
    } else {
        arduboy.audio.off();
    }
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
    eepAddr = max(addr, EEPROM_STORAGE_SPACE_START);
}

// AVR EEPROM API mapped onto the shared engine's private RAM-only 1 KiB EEPROM.
// All targets of this build (AVR upstream, ARM BW16, x86 host) are little-endian,
// so the original 16/32-bit record layout is preserved byte-for-byte.
uint8_t eepRead8(void)
{
    return arduboy.eeprom.read(eepAddr++);
}

uint16_t eepRead16(void)
{
    uint16_t ret = arduboy.eeprom.read(eepAddr) | (uint16_t(arduboy.eeprom.read(eepAddr + 1)) << 8);
    eepAddr += 2;
    return ret;
}

uint32_t eepRead32(void)
{
    uint32_t ret = uint32_t(eepRead16());
    ret |= uint32_t(eepRead16()) << 16;
    return ret;
}

void eepReadBlock(void *p, size_t n)
{
    arduboy.eeprom.read(eepAddr, p, int(n));
    eepAddr += n;
}

void eepWrite8(uint8_t val)
{
    arduboy.eeprom.update(eepAddr++, val);
}

void eepWrite16(uint16_t val)
{
    arduboy.eeprom.update(eepAddr, uint8_t(val & 0xFF));
    arduboy.eeprom.update(eepAddr + 1, uint8_t(val >> 8));
    eepAddr += 2;
}

void eepWrite32(uint32_t val)
{
    eepWrite16(uint16_t(val & 0xFFFF));
    eepWrite16(uint16_t(val >> 16));
}

void eepWriteBlock(const void *p, size_t n)
{
    arduboy.eeprom.write(eepAddr, p, int(n));
    eepAddr += n;
}
