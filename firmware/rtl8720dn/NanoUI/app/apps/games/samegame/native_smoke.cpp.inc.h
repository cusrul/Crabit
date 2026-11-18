// Include-only filename; compile explicitly with -x c++ for the standalone test.
// Standalone host test of actual imported game logic, not the shared engine.
// g++ -std=c++11 -x c++ native_smoke.cpp.inc.h -o <temporary executable>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <algorithm>
using std::max;
// Standalone test supplies the same local min/max API as the actual adapter.
namespace ArduboyPortMath { using std::min; using std::max; using std::abs; }
typedef uint8_t byte;
class __FlashStringHelper;
#define F(x) reinterpret_cast<const __FlashStringHelper *>(x)
#define PROGMEM
#define WHITE 1
#define BLACK 0
#define WIDTH 128
#define HEIGHT 64
#define LEFT_BUTTON 32
#define RIGHT_BUTTON 64
#define UP_BUTTON 8
#define DOWN_BUTTON 4
#define A_BUTTON 2
#define B_BUTTON 1
#define bitRead(x,b) (((x)>>(b))&1)
#define bitSet(x,b) ((x)|=1UL<<(b))
#define bitClear(x,b) ((x)&=~(1UL<<(b)))
#define pgm_read_dword(p) (*(p))
#define pgm_read_byte(p) (*(p))
#define memcpy_P memcpy
void randomSeed(unsigned long) {}
unsigned long micros() { return 0; }
long random(long a, long b) { return a + rand() % (b - a); }
class ArduboyPort {
public:
    uint8_t buf[1024] = {};
    uint8_t buttons = 0;
    struct RamEeprom {
        uint8_t bytes[1024] = {};
        uint8_t read(int a) { assert(a >= 0 && a < 1024); return bytes[a]; }
        void update(int a, uint8_t v) { assert(a >= 0 && a < 1024); bytes[a] = v; }
    } eeprom;
    struct { bool enabled() { return false; } void saveOnOff() {} } audio;
    void clear() { memset(buf, 0, sizeof(buf)); }
    bool nextFrame() { return true; }
    uint8_t buttonsState() { return buttons; }
    uint8_t *getBuffer() { return buf; }
    void drawPixel(int x, int y, uint8_t c) {
        if (x < 0 || x >= 128 || y < 0 || y >= 64) return;
        uint8_t &v = buf[y / 8 * 128 + x];
        if (c) v |= 1 << (y & 7); else v &= ~(1 << (y & 7));
    }
    void fillRect(int x, int y, int w, int h, uint8_t c) {
        for (int i = 0; i < w; ++i) for (int j = 0; j < h; ++j) drawPixel(x+i,y+j,c);
    }
    void fillCircle(int, int, int, uint8_t) {}
    void drawBitmap(int, int, const uint8_t *, uint8_t, uint8_t, uint8_t) {}
    void setFrameRate(uint8_t) {}
    void setRGBled(uint8_t, uint8_t, uint8_t) {}
    void display() {}
};
#include "port.h"

int main() {
    using namespace SameGameSandbox;
    resetGameState();
    setup();
    for (int i = 0; i < 120; ++i) loop();
    assert(mode == MODE_TITLE);
    assert(Title::state == Title::STATE_TITLE);
    assert(getMenuItemCount() == 3);
    arduboy.buttons = B_BUTTON;
    loop();
    arduboy.buttons = 0;
    loop();
    assert(mode == MODE_GAME);
    assert(Game::field.width == 20 && Game::field.height == 10);
    // Deterministic two-group board: exercise original flood fill/removal/undo.
    memset(&Game::field, 0, sizeof(Game::field));
    Game::field.width = 2;
    Game::field.height = 2;
    Game::field.objects[0] = 1 | (1 << 3);
    Game::field.objects[1] = 2 | (2 << 3);
    Game::checkLinkedObjects();
    assert(Game::linkedCount == 2);
    Game::backupField();
    Game::removeLinkedObjects();
    assert(Game::field.objects[0] == 0);
    Game::restoreField();
    assert(Game::field.objects[0] == (1 | (1 << 3)));
    // Original RAM record path, editor, and complete cold UI state on reentry.
    record.hiscore[0] = 1234;
    isRecordDirty = true;
    writeRecord();
    initEditor();
    Editor::counter = 99;
    Game::counter = 99;
    resetGameState();
    assert(Editor::counter == 0 && Game::counter == 0);
    assert(Title::state == Title::STATE_INIT && menuItemCount == 0);
    setup();
    for (int i = 0; i < 120; ++i) loop();
    assert(record.hiscore[0] == 1234);
    assert(getMenuItemCount() == 4); // upstream editor unlock retained
    arduboy.clear();
    arduboy.printEx(0, 0, "SCORE");
    bool hasText = false;
    for (uint8_t v : arduboy.buf) hasText |= v != 0;
    assert(hasText);
    drawObject(123, 59, 0); // last page, exact bottom edge
    puts("SameGame imported logic/reset/undo/editor/RAM-record/font smoke PASS");
}
