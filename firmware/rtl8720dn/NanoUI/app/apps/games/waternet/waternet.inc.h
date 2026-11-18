// Include-only body for the actual Waternet source. The shared compatibility
// layer supplies the display/input boundary; PROGMEM is flat on this target.
// This file is part of one combined sketch translation unit and is deliberately
// not separately compiled.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "blocktiles.h"
#include "selectortiles.h"
#include "congratsscreentiles.h"
#include "titlescreen.h"
#include "congratsscreen.h"

// ---------------------------------------------------------------------------
// Original game constants and state
// ---------------------------------------------------------------------------
constexpr uint8_t maxBoardWidth = 10U;
constexpr uint8_t maxBoardHeight = 8U;
constexpr uint8_t maxBoardBgWidth = 10U;
constexpr uint8_t maxBoardBgHeight = 8U;
constexpr uint8_t maxBoardSize = maxBoardWidth * maxBoardHeight;
constexpr uint8_t tileSize = 8;

constexpr uint8_t gsGame = 0;
constexpr uint8_t gsTitle = 1;
constexpr uint8_t gsLevelSelect = 2;
constexpr uint8_t gsLevelsCleared = 3;
constexpr uint8_t gsHelpRotate = 4;
constexpr uint8_t gsHelpRotate2 = 5;
constexpr uint8_t gsHelpRotate3 = 6;
constexpr uint8_t gsHelpRotateSlide = 7;
constexpr uint8_t gsHelpRotateSlide2 = 8;
constexpr uint8_t gsHelpRotateSlide3 = 9;
constexpr uint8_t gsHelpRotateSlide4 = 10;
constexpr uint8_t gsHelpSlide = 11;
constexpr uint8_t gsHelpSlide2 = 12;
constexpr uint8_t gsHelpSlide3 = 13;
constexpr uint8_t gsIntro = 14;

constexpr uint8_t gsInitDiff = 50;
constexpr uint8_t gsInitGame = gsInitDiff + gsGame;
constexpr uint8_t gsInitTitle = gsInitDiff + gsTitle;
constexpr uint8_t gsInitLevelSelect = gsInitDiff + gsLevelSelect;
constexpr uint8_t gsInitLevelsCleared = gsInitDiff + gsLevelsCleared;
constexpr uint8_t gsInitHelpRotate = gsInitDiff + gsHelpRotate;
constexpr uint8_t gsInitHelpRotate2 = gsInitDiff + gsHelpRotate2;
constexpr uint8_t gsInitHelpRotate3 = gsInitDiff + gsHelpRotate3;
constexpr uint8_t gsInitHelpRotateSlide = gsInitDiff + gsHelpRotateSlide;
constexpr uint8_t gsInitHelpRotateSlide2 = gsInitDiff + gsHelpRotateSlide2;
constexpr uint8_t gsInitHelpRotateSlide3 = gsInitDiff + gsHelpRotateSlide3;
constexpr uint8_t gsInitHelpRotateSlide4 = gsInitDiff + gsHelpRotateSlide4;
constexpr uint8_t gsInitHelpSlide = gsInitDiff + gsHelpSlide;
constexpr uint8_t gsInitHelpSlide2 = gsInitDiff + gsHelpSlide2;
constexpr uint8_t gsInitHelpSlide3 = gsInitDiff + gsHelpSlide3;
constexpr uint8_t gsInitIntro = gsInitDiff + gsIntro;

constexpr uint8_t diffVeryEasy = 0;
constexpr uint8_t diffEasy = 1;
constexpr uint8_t diffNormal = 2;
constexpr uint8_t diffHard = 3;
constexpr uint8_t diffVeryHard = 4;
constexpr uint8_t diffRandom = 5;
constexpr uint8_t diffCount = 6;

constexpr uint8_t gmRotate = 0;
constexpr uint8_t gmSlide = 1;
constexpr uint8_t gmRotateSlide = 2;
constexpr uint8_t gmCount = 3;

constexpr uint8_t mmStartGame = 0;
constexpr uint8_t mmHelp = 1;
constexpr uint8_t mmOptions = 2;
constexpr uint8_t mmCredits = 3;
constexpr uint8_t mmCount = 4;

constexpr uint8_t opMusic = 0;
constexpr uint8_t opSound = 1;
constexpr uint8_t opCount = 2;

constexpr uint8_t tsMainMenu = 0;
constexpr uint8_t tsGameMode = 1;
constexpr uint8_t tsDifficulty = 2;
constexpr uint8_t tsOptions = 3;
constexpr uint8_t tsCredits = 4;

constexpr uint8_t levelCount = 25;
constexpr uint8_t arrowDown = 122;
constexpr uint8_t arrowUp = 120;
constexpr uint8_t arrowLeft = 123;
constexpr uint8_t arrowRight = 121;
constexpr uint8_t leftMenu = 118;
constexpr uint8_t frameRate = 15;

uint8_t startPos, menuPos,
        maxLevel, selectedLevel, boardX, boardY, difficulty,
        gameState, boardWidth, boardHeight, boardSize,
        levelDone, titleStep, gameMode, posAdd,
        debugMode, mainMenu, option, needRedraw;

int16_t selectionX, selectionY;
uint16_t moves;
unsigned long randomSeedGame;
unsigned char level[maxBoardSize];

Arduboy2Compat arduboy;

// ---------------------------------------------------------------------------
// avr-libc random(): kept locally so the 25 deterministic levels are identical
// to the AVR build instead of depending on the host C library. This is the
// Park-Miller generator used by avr-libc's random()/srandom().
// ---------------------------------------------------------------------------
static uint32_t waternetRandomNext = 1;

static void waternetRandomSeed(uint32_t seed) {
    waternetRandomNext = seed;
}

static long waternetRandom(void) {
    int32_t x = static_cast<int32_t>(waternetRandomNext);
    if (x == 0) x = 123459876L;
    const int32_t hi = x / 127773L;
    const int32_t lo = x % 127773L;
    x = 16807L * lo - 2836L * hi;
    if (x < 0) x += 0x7fffffffL;
    waternetRandomNext = static_cast<uint32_t>(x);
    return x;
}

static uint8_t waternetRandom(uint8_t upper) {
    return upper ? static_cast<uint8_t>(waternetRandom() % upper) : 0;
}

// ---------------------------------------------------------------------------
// savestate.cpp: the upstream 13-byte EEPROM payload is kept in RAM only.
// ---------------------------------------------------------------------------
constexpr int eepRomStart = 493;
constexpr uint8_t soundOptionBit = 0U;
constexpr uint8_t musicOptionBit = 1U;

uint32_t levelLocksPacked[3];
uint8_t options = 0;

static uint8_t clearBit8(uint8_t val, uint8_t bit) {
    if (bit > 7) return val;
    return val & (~(1U << bit));
}
static uint8_t setBit8(uint8_t val, uint8_t bit) {
    if (bit > 7) return val;
    return val | (1U << bit);
}
static uint8_t checkBit8(uint8_t val, uint8_t bit) {
    if (bit > 7) return 0;
    return ((val >> bit) & 1U);
}
static uint32_t clearBit32(uint32_t val, uint8_t bit) {
    if (bit > 31) return val;
    return val & (~(1UL << bit));
}
static uint32_t setBit32(uint32_t val, uint8_t bit) {
    if (bit > 31) return val;
    return val | (1UL << bit);
}
static uint8_t checkBit32(uint32_t val, uint8_t bit) {
    if (bit > 31) return 0;
    return ((val >> bit) & 1UL);
}

void packLevelLock(uint8_t mode, uint8_t diff, uint8_t levelValue) {
    uint8_t levelIndex = (mode * diffCount) + diff;
    uint8_t pack = levelIndex / 6;
    uint8_t bit = (levelIndex * 5) - (pack * 30);
    for (uint8_t i = 0; i < 5; i++) {
        if (checkBit8(levelValue, i))
            levelLocksPacked[pack] = setBit32(levelLocksPacked[pack], bit + i);
        else
            levelLocksPacked[pack] = clearBit32(levelLocksPacked[pack], bit + i);
    }
}

uint8_t unPackLevelLock(uint8_t mode, uint8_t diff) {
    uint8_t levelIndex = (mode * diffCount) + diff;
    uint8_t pack = levelIndex / 6;
    uint8_t bit = (levelIndex * 5) - (pack * 30);
    uint8_t result = 0;
    for (uint8_t i = 0; i < 5; i++) {
        if (checkBit32(levelLocksPacked[pack], bit + i))
            result = setBit8(result, i);
    }
    return result;
}

uint8_t validateSaveState() {
    uint8_t levelsUnlocked = 0;
    for (uint8_t j = 0; j < gmCount; j++) {
        for (uint8_t i = 0; i < diffCount; i++) {
            levelsUnlocked = unPackLevelLock(j, i);
            if ((levelsUnlocked == 0) || (levelsUnlocked > levelCount))
                return 0;
        }
    }
    if (options > 3) return 0;
    return 1;
}

void initSaveState() {
    // No EEPROM access on this target. A fresh app object starts with the
    // upstream "invalid save" defaults; subsequent mounts keep the RAM map.
    static bool initialised = false;
    if (!initialised) {
        initialised = true;
        levelLocksPacked[0] = levelLocksPacked[1] = levelLocksPacked[2] = 0;
        options = 0;
    }
    if (!validateSaveState()) {
        levelLocksPacked[0] = 0UL;
        levelLocksPacked[1] = 0UL;
        levelLocksPacked[2] = 0UL;
        for (uint8_t j = 0; j < gmCount; j++)
            for (uint8_t i = 0; i < diffCount; i++)
                packLevelLock(j, i, 1U);
        options = 3;
    }
}

void saveSaveState() {
    // RAM-only by design; the upstream EEPROM.put() pair is intentionally gone.
}

void setMusicOnSaveState(uint8_t value) {
    options = value ? setBit8(options, musicOptionBit) : clearBit8(options, musicOptionBit);
    saveSaveState();
}
uint8_t isMusicOnSaveState() { return checkBit8(options, musicOptionBit); }
void setSoundOnSaveState(uint8_t value) {
    options = value ? setBit8(options, soundOptionBit) : clearBit8(options, soundOptionBit);
    saveSaveState();
}
uint8_t isSoundOnSaveState() { return checkBit8(options, soundOptionBit); }

uint8_t levelUnlocked(uint8_t mode, uint8_t diff, uint8_t levelValue) {
    return (unPackLevelLock(mode, diff) > levelValue);
}
uint8_t lastUnlockedLevel(uint8_t mode, uint8_t diff) {
    return unPackLevelLock(mode, diff);
}
void unlockLevel(uint8_t mode, uint8_t diff, uint8_t levelValue) {
    if (levelValue + 1 > lastUnlockedLevel(mode, diff)) {
        packLevelLock(mode, diff, levelValue + 1);
        saveSaveState();
    }
}

// ---------------------------------------------------------------------------
// sound.cpp: the upstream tone/music state machine is preserved, but the
// hardware output is silent on this device. Calls remain so menu/save flags
// behave exactly as before.
// ---------------------------------------------------------------------------
uint8_t prev_music, music_on, sound_on;

void initSound() { sound_on = 0; }
void initMusic() { music_on = 0; prev_music = 0; }
void soundTimer() {}
void SelectMusic(uint8_t musicFile, uint8_t force = 0) {
    if ((prev_music != musicFile) || force) prev_music = musicFile;
}
void playGameAction() {}
void playMenuAcknowlege() {}
void playMenuBackSound() {}
void playMenuSelectSound() {}
void playErrorSound() {}
void playGameMoveSound() {}
void setMusicOn(uint8_t value) { music_on = value ? 1 : 0; }
void setSoundOn(uint8_t value) { sound_on = value ? 1 : 0; }
uint8_t isMusicOn() { return music_on; }
uint8_t isSoundOn() { return sound_on; }

// ---------------------------------------------------------------------------
// helperfuncs.cpp
// ---------------------------------------------------------------------------
const unsigned char *currentTiles = nullptr;

void set_bkg_tile_xy(uint8_t x, uint8_t y, uint8_t tile) {
    if (currentTiles != nullptr)
        arduboy.drawBitmap(static_cast<int16_t>(x) * 8, static_cast<int16_t>(y) * 8,
                           &currentTiles[2 + (tile * 8)], 8, 8);
}
void set_bkg_data(const unsigned char *tiles) { currentTiles = tiles; }
const unsigned char *get_bkg_data() { return currentTiles; }
void set_bkg_tiles(uint8_t x, uint8_t y, const uint8_t *map) {
    if (map)
        arduboy.drawBitmap(x, y, &map[2],
                           static_cast<uint8_t>(pgm_read_byte(&map[0])),
                           static_cast<uint8_t>(pgm_read_byte(&map[1])));
}
void setBlockTilesAsBackground() { set_bkg_data(blockTiles); }

// ---------------------------------------------------------------------------
// cursor.cpp
// ---------------------------------------------------------------------------
constexpr uint8_t maxCursorFrameCount = (uint8_t)(10 * frameRate / 60);
constexpr uint8_t cursorAnimCount = 2;
constexpr uint8_t cursorNumTiles = 16;

uint8_t cursorFrameCount, cursorFrame, showCursor;
uint8_t spritePos[cursorNumTiles][2];

void move_sprite(uint8_t sprite, uint8_t x, uint8_t y) {
    spritePos[sprite][0] = x;
    spritePos[sprite][1] = y;
}
void drawCursors() {
    if ((showCursor == 0) || (cursorFrame & 1)) return;
    for (uint8_t i = 0; i < cursorNumTiles; i++)
        if (spritePos[i][1] < HEIGHT)
            arduboy.drawBitmap(spritePos[i][0], spritePos[i][1],
                               selectorTiles + 2 + ((i % 8) * 8), 8, 8);
}
uint8_t updateCursorFrame() {
    cursorFrameCount++;
    if (cursorFrameCount >= maxCursorFrameCount) {
        cursorFrame++;
        cursorFrameCount = 0;
        if (cursorFrame >= cursorAnimCount) cursorFrame = 0;
        return 1;
    }
    return 0;
}
void hideCursors() {
    setCursorPos(0, 0, (HEIGHT / 8) + 1);
    setCursorPos(1, 0, (HEIGHT / 8) + 1);
    showCursor = 0;
}
void showCursors() { showCursor = 1; }
void setCursorPos(uint8_t cursorNr, uint8_t xPos, uint8_t yPos) {
    if (cursorNr > 1) return;
    move_sprite((cursorNr << 3) + 0, ((xPos) << 3), ((yPos - 1) << 3));
    move_sprite((cursorNr << 3) + 1, ((xPos + 1) << 3), ((yPos) << 3));
    move_sprite((cursorNr << 3) + 2, ((xPos) << 3), ((yPos + 1) << 3));
    move_sprite((cursorNr << 3) + 3, ((xPos - 1) << 3), ((yPos) << 3));
    move_sprite((cursorNr << 3) + 4, ((xPos + 1) << 3), ((yPos - 1) << 3));
    move_sprite((cursorNr << 3) + 5, ((xPos + 1) << 3), ((yPos + 1) << 3));
    move_sprite((cursorNr << 3) + 6, ((xPos - 1) << 3), ((yPos - 1) << 3));
    move_sprite((cursorNr << 3) + 7, ((xPos - 1) << 3), ((yPos + 1) << 3));
}
void initCursors() {
    hideCursors();
    cursorFrameCount = 0;
    cursorFrame = 0;
}

// ---------------------------------------------------------------------------
// printfuncs.cpp: the original tile-font renderer. The debug-only helpers are
// dropped; debugMode is forced to 0 and no game path calls them.
// ---------------------------------------------------------------------------
template <size_t size>
static size_t formatInteger(char (&array)[size], uint16_t integer) {
    constexpr size_t maxDigits = 10;
    constexpr size_t maxCharacters = (maxDigits + 1);
    static_assert(size >= maxCharacters, "array is too small");
    constexpr size_t lastIndex = (maxCharacters - 1);
    array[lastIndex] = '\0';
    if (integer == 0) {
        array[lastIndex - 1] = '0';
        return 1;
    }
    size_t digits = 0;
    do {
        uint8_t digit = integer % 10;
        integer /= 10;
        ++digits;
        array[lastIndex - digits] = ('0' + digit);
    } while (integer > 0);
    return digits;
}

void printNumber(uint8_t ax, uint8_t ay, uint16_t aNumber, size_t maxDigits) {
    constexpr size_t buffSize = 10;
    char number[buffSize + 1];
    size_t digits = formatInteger(number, aNumber);
    size_t maxFor = digits;
    if (digits > maxDigits) maxFor = maxDigits;
    for (size_t c = 0; c < maxFor; c++) {
        if (number[buffSize - digits + c] == '\0') return;
        set_bkg_tile_xy(ax + (maxDigits - digits) + c, ay,
                        number[buffSize - digits + c] + 32);
    }
}

void printMessage(uint8_t ax, uint8_t ay, const __FlashStringHelper *amsg) {
    const char *p = reinterpret_cast<const char *>(amsg);
    size_t index = 0;
    while (1) {
        char fChar = pgm_read_byte(p++);
        uint8_t tile = 61U;
        switch (fChar) {
            case '\0': return;
            case '[': tile = 70U; break;
            case ']': tile = 64U; break;
            case '<': tile = 73U; break;
            case '>': tile = 67U; break;
            case '+': tile = 63U; break;
            case '*': tile = 62U; break;
            case '|': tile = 69U; break;
            case '#': tile = 65U; break;
            case ':': tile = 116U; break;
            case 'a': tile = 119U; break;
            case 'b': tile = 117U; break;
            default:
                if ((fChar >= 'A') && (fChar <= 'Z')) tile = fChar + 25U;
                if ((fChar >= '0') && (fChar <= '9')) tile = fChar + 32U;
                break;
        }
        set_bkg_tile_xy(ax + index, ay, tile);
        ++index;
    }
}

void printCongratsScreen(uint8_t ax, uint8_t ay, const __FlashStringHelper *amsg) {
    const char *p = reinterpret_cast<const char *>(amsg);
    size_t index = 0;
    while (1) {
        char fChar = pgm_read_byte(p++);
        uint8_t tile = 26U;
        switch (fChar) {
            case '\0': return;
            default:
                if ((fChar >= 'A') && (fChar <= 'Z')) tile = fChar - 'A';
                break;
        }
        set_bkg_tile_xy(ax + index, ay, tile);
        ++index;
    }
}

void printDebugCpuLoad() {}
void printDebug(uint8_t, uint8_t, const __FlashStringHelper *) {}

// ---------------------------------------------------------------------------
// level.cpp: Net/Netslide maze generator, rotate/slide operations and connected
// water flood-fill. Kept as the upstream implementation.
// ---------------------------------------------------------------------------
void moveBlockDown(uint8_t aTile) {
    uint8_t tmp = level[aTile + boardSize - boardWidth];
    for (uint8_t i = boardSize - boardWidth; i != 0; i -= boardWidth)
        level[aTile + i] = level[aTile + i - boardWidth];
    level[aTile] = tmp;
}
void moveBlockUp(uint8_t aTile) {
    uint8_t tmp = level[aTile - boardSize + boardWidth];
    for (uint8_t i = boardSize - boardWidth; i != 0; i -= boardWidth)
        level[aTile - i] = level[aTile - i + boardWidth];
    level[aTile] = tmp;
}
void moveBlockRight(uint8_t aTile) {
    uint8_t tmp = level[aTile + boardWidth - 1];
    memmove(&level[aTile + 1], &level[aTile], boardWidth - 1);
    level[aTile] = tmp;
}
void moveBlockLeft(uint8_t aTile) {
    uint8_t tmp = level[aTile - boardWidth + 1];
    memmove(&level[aTile - boardWidth + 1], &level[aTile - boardWidth + 2], boardWidth - 1);
    level[aTile] = tmp;
}

void rotateBlock(uint8_t aTile) {
    switch (level[aTile]) {
        case 1: case 17: case 33: level[aTile] = 2; break;
        case 2: case 18: case 34: level[aTile] = 4; break;
        case 3: case 19: case 35: level[aTile] = 6; break;
        case 4: case 20: case 36: level[aTile] = 8; break;
        case 5: case 21: case 37: level[aTile] = 10; break;
        case 6: case 22: case 38: level[aTile] = 12; break;
        case 7: case 23: case 39: level[aTile] = 14; break;
        case 8: case 24: case 40: level[aTile] = 1; break;
        case 9: case 25: case 41: level[aTile] = 3; break;
        case 10: case 26: case 42: level[aTile] = 5; break;
        case 11: case 27: case 43: level[aTile] = 7; break;
        case 12: case 28: case 44: level[aTile] = 9; break;
        case 13: case 29: case 45: level[aTile] = 11; break;
        case 14: case 30: case 46: level[aTile] = 13; break;
        default: break;
    }
}

void shuffleSlide(uint8_t aTile) {
    uint8_t rnd = waternetRandom(4);
    switch (rnd) {
        case 0: moveBlockUp((aTile % boardWidth) + boardSize - boardWidth); break;
        case 1: moveBlockDown((aTile % boardWidth)); break;
        case 2: moveBlockLeft(boardWidth - 1 + aTile - (aTile % boardWidth)); break;
        case 3: moveBlockRight(aTile - (aTile % boardWidth)); break;
    }
}
void shuffleRotate(uint8_t aTile) {
    uint8_t rnd = waternetRandom(4);
    for (uint8_t i = 0; i < rnd; i++) rotateBlock(aTile);
}

void shuffleLevel() {
    uint8_t rnd;
    uint8_t j = 0;
    while (j < boardSize) {
        switch (gameMode) {
            case gmRotate: shuffleRotate(j); j++; break;
            case gmSlide: shuffleSlide(j); j += 2; break;
            case gmRotateSlide:
                rnd = waternetRandom(2);
                if (rnd == 0) { shuffleSlide(j); j += 2; }
                else { shuffleRotate(j); j++; }
                break;
        }
    }
}

void handleConnectPoint(uint8_t currentPoint, uint8_t *cellStack, uint8_t *cc) {
    uint8_t lookUpX = currentPoint % boardWidth;
    uint8_t lookUpY = currentPoint / boardWidth;

    if ((lookUpY > 0) && (!(level[currentPoint] & 1))) {
        uint8_t tmp = currentPoint - boardWidth;
        uint8_t tmp2 = level[tmp];
        if (((tmp2 < 16) && (!(tmp2 & 4))) ||
            ((tmp2 > 15) && (!((tmp2 - 16) & 4)))) {
            if (level[currentPoint] < 16) level[currentPoint] += 16;
            if (tmp2 < 16) cellStack[(*cc)++] = tmp;
        }
    }
    if ((lookUpX + 1 < boardWidth) && (!(level[currentPoint] & 2))) {
        uint8_t tmp = currentPoint + 1;
        uint8_t tmp2 = level[tmp];
        if (((tmp2 < 16) && (!(tmp2 & 8))) ||
            ((tmp2 > 15) && (!((tmp2 - 16) & 8)))) {
            if (level[currentPoint] < 16) level[currentPoint] += 16;
            if (tmp2 < 16) cellStack[(*cc)++] = tmp;
        }
    }
    if ((lookUpY + 1 < boardHeight) && (!(level[currentPoint] & 4))) {
        uint8_t tmp = currentPoint + boardWidth;
        uint8_t tmp2 = level[tmp];
        if (((tmp2 < 16) && (!(tmp2 & 1))) ||
            ((tmp2 > 15) && (!((tmp2 - 16) & 1)))) {
            if (level[currentPoint] < 16) level[currentPoint] += 16;
            if (tmp2 < 16) cellStack[(*cc)++] = tmp;
        }
    }
    if ((lookUpX > 0) && (!(level[currentPoint] & 8))) {
        uint8_t tmp = currentPoint - 1;
        uint8_t tmp2 = level[tmp];
        if (((tmp2 < 16) && (!(tmp2 & 2))) ||
            ((tmp2 > 15) && (!((tmp2 - 16) & 2)))) {
            if (level[currentPoint] < 16) level[currentPoint] += 16;
            if (tmp2 < 16) cellStack[(*cc)++] = tmp;
        }
    }
}

void updateConnected() {
    uint8_t cellStack[maxBoardSize + 1];
    for (uint8_t i = 0; i != boardSize; i++) {
        if (level[i] > 31) level[i] -= 32;
        else if (level[i] > 15) level[i] -= 16;
    }
    uint8_t cc = 1;
    handleConnectPoint(startPos, cellStack, &cc);
    while (--cc > 0) {
        if (level[cellStack[cc]] < 16)
            handleConnectPoint(cellStack[cc], cellStack, &cc);
    }
    if (level[startPos] > 15) level[startPos] += 16;
    else if (level[startPos] < 16) level[startPos] += 32;
}

void generateLevel() {
    uint8_t neighbours[4];
    uint8_t cellStack[maxBoardSize + 1];
    uint8_t cc = 0;
    int8_t currentPoint = 0;
    uint8_t visitedRooms = 1;
    uint8_t tmp, tmp2;
    uint8_t selectedNeighbour;
    uint8_t neighboursFound;
    uint8_t lookUpX, lookUpY;
    uint8_t rnd;

    memset(level, 0xfU, boardSize);
    while (visitedRooms != boardSize) {
        neighboursFound = 0;
        lookUpX = currentPoint % boardWidth;
        lookUpY = currentPoint / boardWidth;

        tmp = currentPoint + 1;
        if ((level[tmp] == 0xfU) && (lookUpX + 1 < boardWidth))
            neighbours[neighboursFound++] = tmp;
        tmp = currentPoint - 1;
        if ((level[tmp] == 0xfU) && (lookUpX > 0))
            neighbours[neighboursFound++] = tmp;
        tmp = currentPoint - boardWidth;
        if ((level[tmp] == 0xfU) && (lookUpY > 0))
            neighbours[neighboursFound++] = tmp;
        tmp = currentPoint + boardWidth;
        if ((level[tmp] == 0xfU) && (lookUpY + 1 < boardHeight))
            neighbours[neighboursFound++] = tmp;

        if (neighboursFound == 0) {
            currentPoint = cellStack[--cc];
            continue;
        }
        rnd = waternetRandom(neighboursFound);
        selectedNeighbour = neighbours[rnd];
        tmp = (selectedNeighbour % boardWidth);
        if (tmp > lookUpX) {
            level[selectedNeighbour] &= ~(8);
            level[currentPoint] &= ~(2);
        } else if (tmp < lookUpX) {
            level[selectedNeighbour] &= ~(2);
            level[currentPoint] &= ~(8);
        } else {
            tmp2 = selectedNeighbour / boardWidth;
            if (tmp2 < lookUpY) {
                level[selectedNeighbour] &= ~(4);
                level[currentPoint] &= ~(1);
            } else if (tmp2 > lookUpY) {
                level[selectedNeighbour] &= ~(1);
                level[currentPoint] &= ~(4);
            }
        }
        if (neighboursFound > 1) cellStack[cc++] = currentPoint;
        currentPoint = selectedNeighbour;
        visitedRooms++;
    }
}

uint8_t isLevelDone() {
    for (uint8_t i = 0; i != boardSize; i++)
        if (level[i] < 16) return 0;
    return 1;
}

void initLevel(unsigned long aRandomSeed) {
    levelDone = 0;
    moves = 0;
    if (difficulty != diffRandom)
        waternetRandomSeed(selectedLevel + (difficulty * 500) + (gameMode * 50));
    else
        waternetRandomSeed(static_cast<uint32_t>(aRandomSeed));

    maxLevel = levelCount;
    switch (difficulty) {
        case diffVeryEasy: boardWidth = 5; boardHeight = 5; break;
        case diffEasy: boardWidth = 6; boardHeight = 6; break;
        case diffNormal: boardWidth = 7; boardHeight = 7; break;
        case diffHard: boardWidth = 8; boardHeight = 8; break;
        case diffVeryHard: boardWidth = 10; boardHeight = 8; break;
        case diffRandom: {
            uint8_t rnd = waternetRandom(255);
            boardWidth = 5 + (rnd % (maxBoardWidth - 5 + 1));
            rnd = waternetRandom(255);
            boardHeight = 5 + (rnd % (maxBoardHeight - 5 + 1));
            maxLevel = 0;
            break;
        }
    }
    boardWidth -= posAdd + posAdd;
    boardHeight -= posAdd + posAdd;
    boardSize = boardWidth * boardHeight;
    generateLevel();
    boardX = (maxBoardBgWidth - boardWidth) >> 1;
    boardY = (maxBoardBgHeight - boardHeight) >> 1;
    startPos = (boardWidth >> 1) + (boardHeight >> 1) * (boardWidth);
    selectionX = boardWidth >> 1;
    selectionY = boardHeight >> 1;
    shuffleLevel();
    updateConnected();
}

// ---------------------------------------------------------------------------
// game.cpp
// ---------------------------------------------------------------------------
uint8_t paused, wasMusicOn, wasSoundOn, redrawLevelDoneBit;

void drawGame() {
    if (!paused && !redrawLevelDoneBit) {
        arduboy.clear();
        printMessage(maxBoardBgWidth, 0, F("LEVEL:"));
        printNumber(maxBoardBgWidth + 4, 1, selectedLevel, 2);
        printMessage(maxBoardBgWidth, 2, F("MOVES:"));
        printNumber(maxBoardBgWidth + 1, 3, moves, 5);
        switch (gameMode) {
            case gmRotate:
                printMessage(maxBoardBgWidth, 4, F("a:"));
                printMessage(maxBoardBgWidth, 5, F("ROTATE"));
                break;
            case gmSlide:
                printMessage(maxBoardBgWidth, 4, F("a:"));
                printMessage(maxBoardBgWidth, 5, F("SLIDE"));
                break;
            case gmRotateSlide:
                printMessage(maxBoardBgWidth, 4, F("a:"));
                printMessage(maxBoardBgWidth, 5, F("ROSLID"));
                break;
        }
        printMessage(maxBoardBgWidth, 6, F("b:"));
        printMessage(maxBoardBgWidth, 7, F("BACK"));
        if (gameMode != gmRotate) {
            for (uint8_t x = 0; x != boardWidth; x++) {
                set_bkg_tile_xy(boardX + x, boardY - 1, arrowDown);
                set_bkg_tile_xy(boardX + x, boardY + boardHeight, arrowUp);
            }
            for (uint8_t y = 0; y != boardHeight; y++) {
                set_bkg_tile_xy(boardX - 1, boardY + y, arrowRight);
                set_bkg_tile_xy(boardX + boardWidth, boardY + y, arrowLeft);
            }
        }
        uint16_t i16 = 0;
        for (uint8_t y = 0; y < boardHeight; y++) {
            for (uint8_t x = 0; x < boardWidth; x++)
                set_bkg_tile_xy(boardX + x, boardY + y, level[i16 + x]);
            i16 += boardWidth;
        }
    }
}

void initGame() {
    paused = 0;
    SelectMusic(1);
    setBlockTilesAsBackground();
    initCursors();
    setCursorPos(0, boardX + selectionX, boardY + selectionY);
    showCursors();
    redrawLevelDoneBit = 0;
}

void doPause() {
    paused = 1;
    wasSoundOn = isSoundOn();
    wasMusicOn = isMusicOn();
    setMusicOn(0);
    setSoundOn(0);
    hideCursors();
    arduboy.fillRect(0, ((maxBoardBgHeight >> 1) - 3) * 8, 16 * 8, 6 * 8, BLACK);
    printMessage(0, (maxBoardBgHeight >> 1) - 3, F("[**************]"));
    printMessage(0, (maxBoardBgHeight >> 1) - 2, F("|PLEASE CONFIRM+"));
    printMessage(0, (maxBoardBgHeight >> 1) - 1, F("|              +"));
    printMessage(0, (maxBoardBgHeight >> 1) + 0, F("|   a PLAY     +"));
    printMessage(0, (maxBoardBgHeight >> 1) + 1, F("|   b TO QUIT  +"));
    printMessage(0, (maxBoardBgHeight >> 1) + 2, F("<##############>"));
}

void doUnPause() {
    paused = 0;
    setMusicOn(wasMusicOn);
    setSoundOn(wasSoundOn);
    setCursorPos(0, boardX + selectionX, boardY + selectionY);
    showCursors();
    needRedraw = 1;
}

void game() {
    if (gameState == gsInitGame) {
        initGame();
        gameState -= gsInitDiff;
    }
    if (needRedraw) {
        drawGame();
        drawCursors();
        needRedraw = 0;
    }
    needRedraw = updateCursorFrame();

    if (arduboy.justReleased(DOWN_BUTTON)) {
        if (!levelDone && !paused) {
            playGameMoveSound();
            if (selectionY + 1 < boardHeight + posAdd) selectionY += 1;
            else selectionY = -posAdd;
            needRedraw = 1;
            setCursorPos(0, boardX + selectionX, boardY + selectionY);
        }
    }
    if (arduboy.justReleased(UP_BUTTON)) {
        if (!levelDone && !paused) {
            playGameMoveSound();
            if (selectionY - 1 >= -posAdd) selectionY -= 1;
            else selectionY = boardHeight - 1 + posAdd;
            needRedraw = 1;
            setCursorPos(0, boardX + selectionX, boardY + selectionY);
        }
    }
    if (arduboy.justReleased(RIGHT_BUTTON)) {
        if (!levelDone && !paused) {
            playGameMoveSound();
            if (selectionX + 1 < boardWidth + posAdd) selectionX += 1;
            else selectionX = -posAdd;
            needRedraw = 1;
            setCursorPos(0, boardX + selectionX, boardY + selectionY);
        }
    }
    if (arduboy.justReleased(LEFT_BUTTON)) {
        if (!levelDone && !paused) {
            playGameMoveSound();
            if (selectionX - 1 >= -posAdd) selectionX -= 1;
            else selectionX = boardWidth - 1 + posAdd;
            needRedraw = 1;
            setCursorPos(0, boardX + selectionX, boardY + selectionY);
        }
    }

    if (arduboy.justReleased(A_BUTTON)) {
        if (paused) {
            doUnPause();
            playMenuAcknowlege();
            needRedraw = 1;
        } else if (!levelDone) {
            if ((selectionX > -1) && (selectionX < boardWidth) &&
                (selectionY > -1) && (selectionY < boardHeight)) {
                if (gameMode != gmSlide) {
                    rotateBlock((uint8_t)selectionX + ((uint8_t)selectionY * boardWidth));
                    moves++;
                    playGameAction();
                    needRedraw = 1;
                } else {
                    playErrorSound();
                }
            } else if ((selectionX > -1) && (selectionX < boardWidth)) {
                if (selectionY == -1) {
                    moveBlockDown((uint8_t)selectionX + ((uint8_t)(selectionY + 1) * boardWidth));
                    moves++;
                    playGameAction();
                    needRedraw = 1;
                } else if (selectionY == boardHeight) {
                    moveBlockUp((uint8_t)selectionX + ((uint8_t)(selectionY - 1) * boardWidth));
                    moves++;
                    playGameAction();
                    needRedraw = 1;
                }
            } else if ((selectionY > -1) && (selectionY < boardHeight)) {
                if (selectionX == -1) {
                    moveBlockRight((uint8_t)(selectionX + 1) + ((uint8_t)selectionY * boardWidth));
                    moves++;
                    playGameAction();
                    needRedraw = 1;
                } else if (selectionX == boardWidth) {
                    moveBlockLeft((uint8_t)(selectionX - 1) + ((uint8_t)selectionY * boardWidth));
                    moves++;
                    playGameAction();
                    needRedraw = 1;
                }
            } else {
                playErrorSound();
            }
            updateConnected();
            levelDone = isLevelDone();
            if (levelDone) {
                drawGame();
                SelectMusic(3);
                hideCursors();
                arduboy.fillRect(((16 - 13) >> 1) * 8, ((maxBoardBgHeight >> 1) - 2) * 8, 14 * 8, 5 * 8, BLACK);
                printMessage(((16 - 13) >> 1), (maxBoardBgHeight >> 1) - 2, F("[************]"));
                printMessage(((16 - 13) >> 1), (maxBoardBgHeight >> 1) - 1, F("| LEVEL DONE +"));
                printMessage(((16 - 13) >> 1), (maxBoardBgHeight >> 1) + 0, F("|            +"));
                printMessage(((16 - 13) >> 1), (maxBoardBgHeight >> 1) + 1, F("| a CONTINUE +"));
                printMessage(((16 - 13) >> 1), (maxBoardBgHeight >> 1) + 2, F("<############>"));
                redrawLevelDoneBit = 1;
            }
        } else {
            redrawLevelDoneBit = 0;
            if (difficulty == diffRandom) {
                randomSeedGame = arduboy.generateRandomSeed();
                initLevel(randomSeedGame);
                SelectMusic(1);
                setCursorPos(0, boardX + selectionX, boardY + selectionY);
                showCursors();
                needRedraw = 1;
            } else if (selectedLevel < maxLevel) {
                selectedLevel++;
                unlockLevel(gameMode, difficulty, selectedLevel - 1);
                initLevel(randomSeedGame);
                SelectMusic(1);
                setCursorPos(0, boardX + selectionX, boardY + selectionY);
                showCursors();
                needRedraw = 1;
            } else {
                gameState = gsInitLevelsCleared;
            }
        }
    }

    if (arduboy.justReleased(B_BUTTON)) {
        if (!levelDone) {
            if (!paused) {
                playMenuBackSound();
                doPause();
                needRedraw = 1;
            } else {
                setSoundOn(wasSoundOn);
                hideCursors();
                playMenuBackSound();
                gameState = gsInitLevelSelect;
                doUnPause();
                hideCursors();
                initLevel(randomSeedGame);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Forward declarations used by the menus/help screens.
// ---------------------------------------------------------------------------
void drawTitleScreen();
void initTitleScreen();
void titleScreen();
void drawLevelSelect();
void initLevelSelect();
void levelSelect();
void initLevelsCleared();
void levelsCleared();
void helpLegend(uint8_t nextState);
void helpFinishLevel(uint8_t nextState);
void helpDoSlideRotate(uint8_t nextState);
void helpRotateSlide();
void helpRotateSlide2();
void helpRotateSlide3();
void helpRotateSlide4();
void helpRotate();
void helpRotate2();
void helpRotate3();
void helpSlide();
void helpSlide2();
void helpSlide3();

// ---------------------------------------------------------------------------
// titlescreen.cpp
// ---------------------------------------------------------------------------
void drawTitleScreen() {
    arduboy.clear();
    arduboy.drawBitmap(0, 0, titlescreenBitmap, 128, 40);
    switch (titleStep) {
        case tsMainMenu:
            printMessage(5, 4, F("START"));
            printMessage(5, 5, F("HELP"));
            printMessage(5, 6, F("OPTIONS"));
            printMessage(5, 7, F("CREDITS"));
            break;
        case tsDifficulty:
            printMessage(3, 3, F("VERY EASY"));
            printMessage(3, 4, F("EASY"));
            printMessage(3, 5, F("NORMAL"));
            printMessage(3, 6, F("HARD"));
            if (difficulty <= diffVeryHard) printMessage(3, 7, F("VERY HARD"));
            else printMessage(3, 7, F("RANDOM"));
            break;
        case tsGameMode:
            printMessage(5, 4, F("ROTATE"));
            printMessage(5, 5, F("SLIDE"));
            printMessage(5, 6, F("ROSLID"));
            break;
        case tsCredits:
            printMessage(3, 5, F("CREATED BY"));
            printMessage(2, 6, F("WILLEMS DAVY"));
            printMessage(2, 7, F("JOYRIDER3774"));
            break;
        case tsOptions:
            printMessage(4, 4, isMusicOn() ? F("MUSIC ON") : F("MUSIC OFF"));
            printMessage(4, 5, isSoundOn() ? F("SOUND ON") : F("SOUND OFF"));
            break;
    }
    switch (titleStep) {
        case tsMainMenu: set_bkg_tile_xy(4, 4 + mainMenu, leftMenu); break;
        case tsGameMode: set_bkg_tile_xy(4, 4 + gameMode, leftMenu); break;
        case tsDifficulty:
            if (difficulty >= diffVeryHard) set_bkg_tile_xy(2, 7, leftMenu);
            else set_bkg_tile_xy(2, 3 + difficulty, leftMenu);
            break;
        case tsOptions: set_bkg_tile_xy(2, 4 + option, leftMenu); break;
    }
}

void initTitleScreen() {
    setBlockTilesAsBackground();
    SelectMusic(2);
    needRedraw = 1;
}

void titleScreen() {
    if (gameState == gsInitTitle) {
        initTitleScreen();
        gameState -= gsInitDiff;
    }
    if (needRedraw) {
        drawTitleScreen();
        needRedraw = 0;
    }

    if (arduboy.justReleased(UP_BUTTON)) {
        switch (titleStep) {
            case tsMainMenu: if (mainMenu > mmStartGame) { playMenuSelectSound(); mainMenu--; needRedraw = 1; } break;
            case tsGameMode: if (gameMode > gmRotate) { playMenuSelectSound(); gameMode--; needRedraw = 1; } break;
            case tsDifficulty: if (difficulty > diffVeryEasy) { playMenuSelectSound(); difficulty--; needRedraw = 1; } break;
            case tsOptions: if (option > opMusic) { playMenuSelectSound(); option--; needRedraw = 1; } break;
        }
    }
    if (arduboy.justReleased(DOWN_BUTTON)) {
        switch (titleStep) {
            case tsMainMenu: if (mainMenu < mmCount - 1) { playMenuSelectSound(); mainMenu++; needRedraw = 1; } break;
            case tsGameMode: if (gameMode < gmCount - 1) { playMenuSelectSound(); gameMode++; needRedraw = 1; } break;
            case tsDifficulty: if (difficulty < diffCount - 1) { playMenuSelectSound(); difficulty++; needRedraw = 1; } break;
            case tsOptions: if (option < opCount - 1) { playMenuSelectSound(); option++; needRedraw = 1; } break;
        }
    }
    if (arduboy.justReleased(B_BUTTON)) {
        switch (titleStep) {
            case tsOptions:
            case tsCredits:
                titleStep = tsMainMenu;
                playMenuBackSound();
                needRedraw = 1;
                break;
            case tsGameMode:
            case tsDifficulty:
                titleStep--;
                playMenuBackSound();
                needRedraw = 1;
                break;
        }
    }
    if (arduboy.justReleased(A_BUTTON)) {
        playMenuAcknowlege();
        switch (mainMenu) {
            case mmOptions:
                if (titleStep != tsOptions) {
                    titleStep = tsOptions;
                    needRedraw = 1;
                } else {
                    switch (option) {
                        case opMusic:
                            setMusicOn(!isMusicOn());
                            setMusicOnSaveState(isMusicOn());
                            needRedraw = 1;
                            break;
                        case opSound:
                            setSoundOn(!isSoundOn());
                            setSoundOnSaveState(isSoundOn());
                            needRedraw = 1;
                            break;
                    }
                }
                break;
            case mmCredits:
                if (titleStep != tsCredits) {
                    titleStep = tsCredits;
                    needRedraw = 1;
                } else {
                    titleStep = tsMainMenu;
                    needRedraw = 1;
                }
                break;
            case mmHelp:
                if (titleStep < tsGameMode) {
                    titleStep++;
                    needRedraw = 1;
                } else {
                    switch (gameMode) {
                        case gmRotate: gameState = gsInitHelpRotate; break;
                        case gmSlide: gameState = gsInitHelpSlide; break;
                        case gmRotateSlide: gameState = gsInitHelpRotateSlide; break;
                    }
                }
                break;
            case mmStartGame:
                if (titleStep < tsDifficulty) {
                    titleStep++;
                    needRedraw = 1;
                } else {
                    if (difficulty == diffRandom) selectedLevel = 1;
                    else selectedLevel = lastUnlockedLevel(gameMode, difficulty);
                    posAdd = (gameMode == gmRotate) ? 0 : 1;
                    randomSeedGame = arduboy.generateRandomSeed();
                    initLevel(randomSeedGame);
                    gameState = gsInitLevelSelect;
                }
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// levelselect.cpp
// ---------------------------------------------------------------------------
void drawLevelSelect() {
    arduboy.clear();
    printMessage(maxBoardBgWidth, 0, F("LEVEL:"));
    printNumber(maxBoardBgWidth + 4, 1, selectedLevel, 2);
    printMessage(maxBoardBgWidth, 6, F("b:"));
    printMessage(maxBoardBgWidth, 7, F("BACK"));
    printMessage(maxBoardBgWidth, 4, F("a:"));
    printMessage(maxBoardBgWidth, 5, F("PLAY"));
    uint8_t tmpUnlocked = levelUnlocked(gameMode, difficulty, selectedLevel - 1);
    printMessage(maxBoardBgWidth, 2, tmpUnlocked ? F("OPEN") : F("LOCKED"));
    if (gameMode != gmRotate) {
        for (uint8_t x = 0; x != boardWidth; x++) {
            set_bkg_tile_xy(boardX + x, boardY - 1, arrowDown);
            set_bkg_tile_xy(boardX + x, boardY + boardHeight, arrowUp);
        }
        for (uint8_t y = 0; y != boardHeight; y++) {
            set_bkg_tile_xy(boardX - 1, boardY + y, arrowRight);
            set_bkg_tile_xy(boardX + boardWidth, boardY + y, arrowLeft);
        }
    }
    uint16_t i16 = 0;
    for (uint8_t y = 0; y < boardHeight; y++) {
        for (uint8_t x = 0; x < boardWidth; x++)
            set_bkg_tile_xy(boardX + x, boardY + y, level[i16 + x]);
        i16 += boardWidth;
    }
}

void initLevelSelect() {
    setBlockTilesAsBackground();
    SelectMusic(2);
    needRedraw = 1;
}

void levelSelect() {
    if (gameState == gsInitLevelSelect) {
        initLevelSelect();
        gameState -= gsInitDiff;
    }
    if (needRedraw) {
        drawLevelSelect();
        needRedraw = 0;
    }
    uint8_t tmpUnlocked = levelUnlocked(gameMode, difficulty, selectedLevel - 1);
    if (arduboy.justReleased(B_BUTTON)) {
        playMenuBackSound();
        gameState = gsInitTitle;
    }
    if (arduboy.justReleased(A_BUTTON)) {
        if (tmpUnlocked) {
            gameState = gsInitGame;
            playMenuAcknowlege();
        } else {
            playErrorSound();
        }
    }
    if (arduboy.justReleased(LEFT_BUTTON)) {
        if (difficulty == diffRandom) {
            playMenuSelectSound();
            randomSeedGame = arduboy.generateRandomSeed();
            initLevel(randomSeedGame);
            needRedraw = 1;
        } else if (selectedLevel > 1) {
            playMenuSelectSound();
            selectedLevel--;
            initLevel(randomSeedGame);
            needRedraw = 1;
        }
    }
    if (arduboy.justReleased(RIGHT_BUTTON)) {
        if (difficulty == diffRandom) {
            playMenuSelectSound();
            randomSeedGame = arduboy.generateRandomSeed();
            initLevel(randomSeedGame);
            needRedraw = 1;
        } else if (selectedLevel < maxLevel) {
            playMenuSelectSound();
            selectedLevel++;
            initLevel(randomSeedGame);
            needRedraw = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// levelscleared.cpp
// ---------------------------------------------------------------------------
void initLevelsCleared() {
    set_bkg_data(congratsScreenTiles);
    arduboy.clear();
    arduboy.drawBitmap(0, 0, congratsScreenBitmap, 128, 64);
    switch (difficulty) {
        case diffVeryEasy: printCongratsScreen(0, 3, F("VERY EASY LEVELS")); break;
        case diffEasy: printCongratsScreen(3, 3, F("EASY LEVELS")); break;
        case diffNormal: printCongratsScreen(2, 3, F("NORMAL LEVELS")); break;
        case diffHard: printCongratsScreen(3, 3, F("HARD LEVELS")); break;
        case diffVeryHard: printCongratsScreen(0, 3, F("VERY HARD LEVELS")); break;
    }
    SelectMusic(4);
}

void levelsCleared() {
    if (gameState == gsInitLevelsCleared) {
        initLevelsCleared();
        gameState -= gsInitDiff;
    }
    if (arduboy.justReleased(A_BUTTON) || arduboy.justReleased(B_BUTTON)) {
        playMenuAcknowlege();
        titleStep = tsMainMenu;
        gameState = gsInitTitle;
    }
}

// ---------------------------------------------------------------------------
// intro.cpp
// ---------------------------------------------------------------------------
uint16_t frames;
float titlePosY;
constexpr uint8_t frameDelay = 16 * frameRate / 15;

void initIntro() {
    setBlockTilesAsBackground();
    titlePosY = HEIGHT;
    frames = 0;
}

void intro() {
    if (gameState == gsInitIntro) {
        initIntro();
        gameState -= gsInitDiff;
    }
    frames++;
    arduboy.clear();
    if (frames < frameDelay) {
        printMessage(4 >> 1, 4, F("WILLEMS DAVY"));
    } else if (frames < frameDelay * 2) {
        printMessage(8 >> 1, 4, F("PRESENTS"));
    } else {
        arduboy.drawBitmap(0, static_cast<int16_t>(titlePosY), titlescreenBitmap, 128, 40);
        if (titlePosY > 0) titlePosY -= (float)30 / (float)frameRate;
        else gameState = gsInitTitle;
    }
    if (arduboy.justReleased(A_BUTTON) || arduboy.justReleased(B_BUTTON))
        gameState = gsInitTitle;
}

// ---------------------------------------------------------------------------
// helpscreens.cpp
// ---------------------------------------------------------------------------
void inithelpLegend() {
    setBlockTilesAsBackground();
    SelectMusic(2);
    needRedraw = 1;
}
void helpLegend(uint8_t nextState) {
    if ((gameState == gsInitHelpSlide) ||
        (gameState == gsInitHelpRotate) ||
        (gameState == gsInitHelpRotateSlide)) {
        inithelpLegend();
        gameState -= gsInitDiff;
    }
    if (needRedraw) {
        arduboy.clear();
        switch (gameState) {
            case gsHelpSlide: printMessage(2, 0, F("HELP: SLIDE")); break;
            case gsHelpRotate: printMessage(2, 0, F("HELP: ROTATE")); break;
            case gsHelpRotateSlide: printMessage(2, 0, F("HELP: ROSLID")); break;
        }
        set_bkg_tile_xy(0, 1, 33);
        printMessage(1, 1, F(":WATER SOURCE"));
        set_bkg_tile_xy(0, 2, 11);
        set_bkg_tile_xy(1, 2, 6);
        set_bkg_tile_xy(2, 2, 12);
        printMessage(3, 2, F(":NOT FILLED"));
        set_bkg_tile_xy(0, 3, 27);
        set_bkg_tile_xy(1, 3, 22);
        set_bkg_tile_xy(2, 3, 28);
        printMessage(3, 3, F(":FILLED"));
        if ((gameState == gsHelpRotateSlide) || (gameState == gsHelpSlide)) {
            set_bkg_tile_xy(0, 4, 121);
            printMessage(1, 4, F(":SLID ROW RIGHT"));
            set_bkg_tile_xy(0, 5, 123);
            printMessage(1, 5, F(":SLID ROW LEFT"));
            set_bkg_tile_xy(0, 6, 122);
            printMessage(1, 6, F(":SLID COL DOWN"));
            set_bkg_tile_xy(0, 7, 120);
            printMessage(1, 7, F(":SLID COL UP"));
        }
        needRedraw = 0;
    }
    if (arduboy.justReleased(A_BUTTON)) {
        playMenuAcknowlege();
        gameState = nextState;
    }
}

void initHelpFinishLevel() {
    setBlockTilesAsBackground();
    SelectMusic(2);
    needRedraw = 1;
}
void helpFinishLevel(uint8_t nextState) {
    if ((gameState == gsInitHelpSlide2) ||
        (gameState == gsInitHelpRotate2) ||
        (gameState == gsInitHelpRotateSlide2)) {
        initHelpFinishLevel();
        gameState -= gsInitDiff;
    }
    if (needRedraw) {
        arduboy.clear();
        switch (gameState) {
            case gsHelpSlide2: printMessage(2, 0, F("HELP: SLIDE")); break;
            case gsHelpRotate2: printMessage(2, 0, F("HELP: ROTATE")); break;
            case gsHelpRotateSlide2: printMessage(2, 0, F("HELP: ROSLID")); break;
        }
        printMessage(0, 2, F("LEVEL FINISH:"));
        if ((gameState == gsHelpSlide2) || (gameState == gsHelpRotateSlide2)) {
            set_bkg_tile_xy(2, 3, 122);
            set_bkg_tile_xy(3, 3, 122);
            set_bkg_tile_xy(4, 3, 122);
            set_bkg_tile_xy(1, 4, 121);
            set_bkg_tile_xy(5, 4, 123);
            set_bkg_tile_xy(1, 5, 121);
            set_bkg_tile_xy(5, 5, 123);
            set_bkg_tile_xy(1, 6, 121);
            set_bkg_tile_xy(5, 6, 123);
            set_bkg_tile_xy(2, 7, 120);
            set_bkg_tile_xy(3, 7, 120);
            set_bkg_tile_xy(4, 7, 120);
        }
        set_bkg_tile_xy(2, 4, 25);
        set_bkg_tile_xy(3, 4, 23);
        set_bkg_tile_xy(4, 4, 27);
        printMessage(7, 4, F("ALL WATER"));
        set_bkg_tile_xy(2, 5, 28);
        set_bkg_tile_xy(3, 5, 33);
        set_bkg_tile_xy(4, 5, 22);
        printMessage(7, 5, F("PIPES ARE"));
        set_bkg_tile_xy(2, 6, 29);
        set_bkg_tile_xy(3, 6, 20);
        set_bkg_tile_xy(4, 6, 23);
        printMessage(7, 6, F("FILLED"));
        needRedraw = 0;
    }
    if (arduboy.justReleased(A_BUTTON)) {
        playMenuAcknowlege();
        gameState = nextState;
    }
}

void initHelpDoSlideRotate() {
    setBlockTilesAsBackground();
    SelectMusic(2);
    initCursors();
    if ((gameState == gsInitHelpRotateSlide4) || (gameState == gsInitHelpSlide3)) {
        setCursorPos(0, 0, 5);
        setCursorPos(1, 11, 5);
    } else {
        setCursorPos(0, 1, 4);
        setCursorPos(1, 12, 4);
    }
    showCursors();
    needRedraw = 1;
}

void helpDoSlideRotate(uint8_t nextState) {
    if ((gameState == gsInitHelpSlide3) ||
        (gameState == gsInitHelpRotate3) ||
        (gameState == gsInitHelpRotateSlide3) ||
        (gameState == gsInitHelpRotateSlide4)) {
        initHelpDoSlideRotate();
        gameState -= gsInitDiff;
    }
    if (needRedraw) {
        arduboy.clear();
        switch (gameState) {
            case gsHelpSlide3: printMessage(2, 0, F("HELP: SLIDE")); break;
            case gsHelpRotate3: printMessage(2, 0, F("HELP: ROTATE")); break;
            case gsHelpRotateSlide3:
            case gsHelpRotateSlide4: printMessage(2, 0, F("HELP: ROSLID")); break;
        }
        if ((gameState == gsHelpRotateSlide3) || (gameState == gsHelpRotate3))
            printMessage(5, 2, F("ROTATE"));
        else
            printMessage(6, 2, F("SLIDE"));
        set_bkg_tile_xy(6, 5, 119);
        set_bkg_tile_xy(9, 5, 118);
        if ((gameState == gsHelpSlide3) ||
            (gameState == gsHelpRotateSlide3) ||
            (gameState == gsHelpRotateSlide4)) {
            set_bkg_tile_xy(1, 3, 122);
            set_bkg_tile_xy(2, 3, 122);
            set_bkg_tile_xy(3, 3, 122);
            set_bkg_tile_xy(0, 4, 121);
            set_bkg_tile_xy(4, 4, 123);
            set_bkg_tile_xy(0, 5, 121);
            set_bkg_tile_xy(4, 5, 123);
            set_bkg_tile_xy(0, 6, 121);
            set_bkg_tile_xy(4, 6, 123);
            set_bkg_tile_xy(1, 7, 120);
            set_bkg_tile_xy(2, 7, 120);
            set_bkg_tile_xy(3, 7, 120);
            set_bkg_tile_xy(12, 3, 122);
            set_bkg_tile_xy(13, 3, 122);
            set_bkg_tile_xy(14, 3, 122);
            set_bkg_tile_xy(11, 4, 121);
            set_bkg_tile_xy(15, 4, 123);
            set_bkg_tile_xy(11, 5, 121);
            set_bkg_tile_xy(15, 5, 123);
            set_bkg_tile_xy(11, 6, 121);
            set_bkg_tile_xy(15, 6, 123);
            set_bkg_tile_xy(12, 7, 120);
            set_bkg_tile_xy(13, 7, 120);
            set_bkg_tile_xy(14, 7, 120);
        }
        if ((gameState == gsHelpRotate3) || (gameState == gsHelpRotateSlide3)) {
            set_bkg_tile_xy(1, 4, 12);
            set_bkg_tile_xy(2, 4, 7);
            set_bkg_tile_xy(3, 4, 27);
            set_bkg_tile_xy(1, 5, 28);
            set_bkg_tile_xy(2, 5, 33);
            set_bkg_tile_xy(3, 5, 22);
            set_bkg_tile_xy(1, 6, 29);
            set_bkg_tile_xy(2, 6, 20);
            set_bkg_tile_xy(3, 6, 23);
        } else {
            set_bkg_tile_xy(1, 4, 9);
            set_bkg_tile_xy(2, 4, 7);
            set_bkg_tile_xy(3, 4, 11);
            set_bkg_tile_xy(1, 5, 17);
            set_bkg_tile_xy(2, 5, 38);
            set_bkg_tile_xy(3, 5, 12);
            set_bkg_tile_xy(1, 6, 13);
            set_bkg_tile_xy(2, 6, 4);
            set_bkg_tile_xy(3, 6, 7);
        }
        set_bkg_tile_xy(12, 4, 25);
        set_bkg_tile_xy(13, 4, 23);
        set_bkg_tile_xy(14, 4, 27);
        set_bkg_tile_xy(12, 5, 28);
        set_bkg_tile_xy(13, 5, 33);
        set_bkg_tile_xy(14, 5, 22);
        set_bkg_tile_xy(12, 6, 29);
        set_bkg_tile_xy(13, 6, 20);
        set_bkg_tile_xy(14, 6, 23);
        drawCursors();
        needRedraw = 0;
    }
    needRedraw = updateCursorFrame();
    if (arduboy.justReleased(A_BUTTON)) {
        playMenuAcknowlege();
        gameState = nextState;
        hideCursors();
    }
}

void helpRotateSlide() { helpLegend(gsInitHelpRotateSlide2); }
void helpRotateSlide2() { helpFinishLevel(gsInitHelpRotateSlide3); }
void helpRotateSlide3() { helpDoSlideRotate(gsInitHelpRotateSlide4); }
void helpRotateSlide4() { helpDoSlideRotate(gsInitTitle); }
void helpRotate() { helpLegend(gsInitHelpRotate2); }
void helpRotate2() { helpFinishLevel(gsInitHelpRotate3); }
void helpRotate3() { helpDoSlideRotate(gsInitTitle); }
void helpSlide() { helpLegend(gsInitHelpSlide2); }
void helpSlide2() { helpFinishLevel(gsInitHelpSlide3); }
void helpSlide3() { helpDoSlideRotate(gsInitTitle); }

// ---------------------------------------------------------------------------
// waternet.ino / commonvars.cpp
// ---------------------------------------------------------------------------
void setup() {
    option = 0;
    debugMode = 0;
    difficulty = diffNormal;
    selectedLevel = 1;
    mainMenu = mmStartGame;
    gameState = gsInitIntro;
    titleStep = tsMainMenu;
    gameMode = gmRotate;
    posAdd = 0;
    arduboy.begin();
    arduboy.setFrameRate(frameRate);
    initSaveState();
    initSound();
    initMusic();
    setMusicOn(isMusicOnSaveState() && arduboy.audio.enabled());
    setSoundOn(isSoundOnSaveState() && arduboy.audio.enabled());
}

void loop() {
    if (!arduboy.nextFrame()) return;
    soundTimer();
    arduboy.pollButtons();
    switch (gameState) {
        case gsInitTitle:
        case gsTitle: titleScreen(); break;
        case gsInitLevelSelect:
        case gsLevelSelect: levelSelect(); break;
        case gsInitGame:
        case gsGame: game(); break;
        case gsInitLevelsCleared:
        case gsLevelsCleared: levelsCleared(); break;
        case gsInitHelpSlide:
        case gsHelpSlide: helpSlide(); break;
        case gsInitHelpSlide2:
        case gsHelpSlide2: helpSlide2(); break;
        case gsInitHelpSlide3:
        case gsHelpSlide3: helpSlide3(); break;
        case gsHelpRotateSlide:
        case gsInitHelpRotateSlide: helpRotateSlide(); break;
        case gsInitHelpRotateSlide2:
        case gsHelpRotateSlide2: helpRotateSlide2(); break;
        case gsInitHelpRotateSlide3:
        case gsHelpRotateSlide3: helpRotateSlide3(); break;
        case gsInitHelpRotateSlide4:
        case gsHelpRotateSlide4: helpRotateSlide4(); break;
        case gsInitHelpRotate:
        case gsHelpRotate: helpRotate(); break;
        case gsInitHelpRotate2:
        case gsHelpRotate2: helpRotate2(); break;
        case gsInitHelpRotate3:
        case gsHelpRotate3: helpRotate3(); break;
        case gsInitIntro:
        case gsIntro: intro(); break;
    }
    arduboy.display();
}
