#ifndef CRABIT_ARDUBOY2_COMPAT_H
#define CRABIT_ARDUBOY2_COMPAT_H

// Crabit Arduboy compatibility layer.
//
// This is intentionally independent from the legacy `Arduboy2` class in
// `legacy/ArduboyCore.h`. Legacy games keep their accepted implementation;
// new ports use this layer and can be added without inheriting its broken
// Canvas drawing or its implicit IMU mapping.
//
// Implemented: framebuffer graphics, 5x7 text, buttons/edges, 60 Hz pacing,
// Sprites, mono tone/sequence audio through the STM32 PWM service, and
// per-engine RAM EEPROM.
// Not implemented: persistent EEPROM, compressed bitmaps, RGB LEDs.

#include "../../../src/NanoUI.h"
#include "ArduboyAudioCompat.h"
#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef PROGMEM
#define PROGMEM
#endif
#ifndef pgm_read_byte
#define pgm_read_byte(address) (*(const uint8_t *)(address))
#endif
#ifndef pgm_read_word
#define pgm_read_word(address) (*(const uint16_t *)(address))
#endif
#ifndef pgm_read_dword
#define pgm_read_dword(address) (*(const uint32_t *)(address))
#endif
#ifndef pgm_read_ptr
#define pgm_read_ptr(address) (*(address))
#endif
#ifndef strnlen_P
#define strnlen_P strnlen
#endif
#ifndef PI
#define PI 3.14159265358979323846
#endif
#ifndef constrain
#define constrain(value, low, high) ((value) < (low) ? (low) : ((value) > (high) ? (high) : (value)))
#endif
// Host fallback only: the real Arduino core already defines these macros.
// bitToggle is not an Arduino macro, so expose it as a namespaced function
// that survives include-order and undef churn in older ports.
#ifndef bitRead
#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) ((bitvalue) ? bitSet(value, bit) : bitClear(value, bit))
#endif
#ifndef F
class __FlashStringHelper;
#define F(text) reinterpret_cast<const __FlashStringHelper *>(text)
#endif

#undef WHITE
#undef BLACK
#define WHITE 1
#define BLACK 0
#define INVERT 2
#define WIDTH 128
#define HEIGHT 64

#define LEFT_BUTTON  0x20
#define RIGHT_BUTTON 0x40
#define UP_BUTTON    0x08
#define DOWN_BUTTON  0x04
#define A_BUTTON     0x02
#define B_BUTTON     0x01

// ArduboyTones note frequencies (ArduboyTonesPitches.h). "H" marks the
// high-volume bit; the STM32 PWM has no volume control, so the adapter strips
// it and these notes keep their pitch.
#define Arduboy2Base Arduboy2Compat
#define NOTE_C1 33
#define NOTE_C2 65
#define NOTE_CS2 69
#define NOTE_D2 73
#define NOTE_E2 82
#define NOTE_F2 87
#define NOTE_G1 49
#define NOTE_G2 98
#define NOTE_G3 196
#define NOTE_G4 392
#define NOTE_GS2 104
#define NOTE_GS3 208
#define NOTE_GS4 415
#define NOTE_GS5 831
#define NOTE_A2 110
#define NOTE_B2 123
#define NOTE_C3 131
#define NOTE_CS3 139
// NOTE_CS3H is NOTE_CS3 + TONE_HIGH_VOLUME in upstream ArduboyTonesPitches.h.
#define NOTE_CS3H (NOTE_CS3 | 0x8000)
#define NOTE_D3 147
#define NOTE_E3 165
#define NOTE_F3 175
#define NOTE_A3 220
#define NOTE_B3 247
#define NOTE_C4 262
#define NOTE_CS4 277
#define NOTE_D4 294
#define NOTE_E4 330
#define NOTE_F4 349
#define NOTE_A4 440
#define NOTE_B4 494
#define NOTE_C5 523
#define NOTE_CS5 554
#define NOTE_D5 587
#define NOTE_E5 659
#define NOTE_F5 698
#define NOTE_G5 784
#define NOTE_A5 880
#define NOTE_B5 988
#define NOTE_C6 1047
#define NOTE_CS6 1109
#define NOTE_CS7 2218
#define NOTE_E6 1319
#define TONES_END 0x8000
#define TONES_REPEAT 0x8001
#define RGB_ON 0
#define RGB_OFF 1

#define ALIGN_LEFT   0
#define ALIGN_CENTER 1
#define ALIGN_RIGHT  2

static const uint8_t *arduboyFont5x7();

namespace ArduboyMath {
template<class A, class B> inline auto min(A a, B b) -> decltype(true ? A() : B()) {
    return a < b ? a : b;
}
template<class A, class B> inline auto max(A a, B b) -> decltype(true ? A() : B()) {
    return a > b ? a : b;
}
template<class T> inline auto abs(T value) -> decltype(+value) {
    return value < 0 ? -value : value;
}
}
// Preserve the name used by the first three ports.
namespace ArduboyPortMath = ArduboyMath;

// Arduboy2's collision geometry. Keep it namespaced: NanoUI already owns
// global Point/Rect types (left/top/right/bottom), and a port that wants the
// original Arduboy names must alias them inside its own sandbox.
namespace CrabitArduboy {
inline bool readBit(uint32_t value, uint8_t bit) { return ((value >> bit) & 1U) != 0; }
inline void toggleBit(uint8_t &value, uint8_t bit) { value ^= uint8_t(1U << bit); }

struct Point {
    int16_t x = 0;
    int16_t y = 0;
    Point() {}
    Point(int16_t px, int16_t py) : x(px), y(py) {}
};
struct Rect {
    int16_t x = 0;
    int16_t y = 0;
    uint8_t width = 0;
    uint8_t height = 0;
    Rect() {}
    Rect(int16_t px, int16_t py, uint8_t w, uint8_t h)
        : x(px), y(py), width(w), height(h) {}
};
} // namespace CrabitArduboy

class Arduboy2Compat;
static Arduboy2Compat *g_arduboyCompatActive = nullptr;

class Arduboy2Compat {
public:
    uint8_t sBuffer[1024];
    uint32_t frameCount = 0;

    struct RamEeprom {
        uint8_t bytes[1024];
        // Zero-save ports declare that the game never touches EEPROM. Their
        // owner points this at a per-State counter; any access then fails a
        // regression check instead of silently discarding a save.
        uint16_t *unexpectedAccess = nullptr;
        RamEeprom() { memset(bytes, 0xFF, sizeof(bytes)); }
        uint8_t read(int address) const {
            countUnexpected();
            return address >= 0 && address < 1024 ? bytes[address] : 0xFF;
        }
        void update(int address, uint8_t value) {
            countUnexpected();
            if (address >= 0 && address < 1024) bytes[address] = value;
        }
        void countUnexpected() const {
            if (unexpectedAccess) ++*unexpectedAccess;
        }
        void write(int address, uint8_t value) { update(address, value); }
        void read(int address, void *output, int count) const {
            uint8_t *p = static_cast<uint8_t *>(output);
            for (int i = 0; i < count; ++i) p[i] = read(address + i);
        }
        void write(int address, const void *input, int count) {
            const uint8_t *p = static_cast<const uint8_t *>(input);
            for (int i = 0; i < count; ++i) update(address + i, p[i]);
        }
        int length() const { return 1024; }
    } eeprom; // RAM only: never map AVR addresses onto BW16 settings/FTL.

    // Real shared mono audio flag. Static so upstream code can pass
    // `arduboy.audio.enabled` to ArduboyTones. There is one live foreground
    // owner, so the flag follows the mounted App and is reset on mount.
    struct AudioCompat {
        static bool enabled() { return CrabitArduboyAudio::enabled(); }
        void begin() { CrabitArduboyAudio::setEnabled(true); }
        void on() { CrabitArduboyAudio::setEnabled(true); }
        void off() { CrabitArduboyAudio::setEnabled(false); }
        void toggle() { CrabitArduboyAudio::setEnabled(!enabled()); }
        void saveOnOff() {} // No audio-flag Flash persistence; RAM only.
    } audio;

    Arduboy2Compat() { g_arduboyCompatActive = this; }
    ~Arduboy2Compat() {
        // Only the most recent engine owns the compatibility "active" pointer;
        // an on-demand port must not leave a dangling pointer behind for
        // SpritesCompat's default constructor.
        if (g_arduboyCompatActive == this) g_arduboyCompatActive = nullptr;
    }

    void attachCanvas(Canvas *canvas) { _canvas = canvas; }
    uint8_t *getBuffer() { return sBuffer; }
    const uint8_t *getBuffer() const { return sBuffer; }

    void resetRuntime() {
        hardware = current = previous = 0;
        frameCount = 0;
        rate = 60;
        phase = 0;
        lastTime = 0;
        started = false;
        cursor_x = cursor_y = 0;
        textSize = 1;
        textWrap = true;
        textColor = WHITE;
        textBackground = BLACK;
        polled = false;
        clear();
    }

    void begin() { beginNoLogo(); }
    void beginNoLogo() { resetRuntime(); }
    void boot() {}
    void bootLogo() {}
    void flashlight() {}
    void systemButtons() {}
    void blank() { clear(); }
    void clear() { memset(sBuffer, 0, sizeof(sBuffer)); }
    void fillScreen(uint8_t color) {
        if (color == INVERT) {
            for (unsigned i = 0; i < sizeof(sBuffer); ++i) sBuffer[i] ^= 0xFF;
        } else {
            memset(sBuffer, color ? 0xFF : 0, sizeof(sBuffer));
        }
    }

    void display() {
        if (!_canvas) return;
        _canvas->clear();
        for (int y = 0; y < HEIGHT; ++y)
            for (int x = 0; x < WIDTH; ++x)
                if (sBuffer[(y / 8) * WIDTH + x] & (1U << (y & 7)))
                    _canvas->drawPixel(x, y, WHITE);
        _canvas->display();
    }
    void display(bool clearAfter) {
        display();
        if (clearAfter) clear();
    }

    void setFrameRate(uint8_t fps) { rate = fps ? fps : 1; phase = 0; started = false; }
    bool nextFrame() {
        const uint32_t now = static_cast<uint32_t>(millis());
        if (started) {
            const uint32_t elapsed = now - lastTime;
            lastTime = now;
            const uint64_t ticks = static_cast<uint64_t>(phase) + static_cast<uint64_t>(elapsed) * rate;
            if (ticks < 1000) {
                phase = static_cast<uint32_t>(ticks);
                return false;
            }
            phase = static_cast<uint32_t>(ticks % 1000); // Never catch up in a tight loop.
        } else {
            started = true;
            lastTime = now;
        }
        ++frameCount;
        pollButtons();
        return true;
    }
    bool nextFrameDEV() { return nextFrame(); }
    bool everyXFrames(uint8_t count) const { return count && frameCount % count == 0; }
    int cpuLoad() const { return 0; }

    void updateHwButton(uint8_t mask) { hardware = mask; }
    void pollButtons() {
        // Multiple polls in one game frame must not consume the edge.
        if (polled && polledFrame == frameCount) return;
        previous = current;
        current = hardware;
        polledFrame = frameCount;
        polled = true;
    }
    uint8_t buttonsState() const { return current; }
    bool pressed(uint8_t mask) const { return (current & mask) == mask; }
    bool notPressed(uint8_t mask) const { return !(current & mask); }
    bool justPressed(uint8_t mask) const { return (current & ~previous & mask) != 0; }
    bool justReleased(uint8_t mask) const { return (~current & previous & mask) != 0; }
    bool buttonDown(uint8_t mask) const { return justPressed(mask); }
    bool buttonPressed(uint8_t mask) const { return (current & mask) != 0; }
    bool buttonUp(uint8_t mask) const { return justReleased(mask); }

    void drawPixel(int16_t x, int16_t y, uint8_t color = WHITE) {
        if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return;
        uint8_t &value = sBuffer[(y / 8) * WIDTH + x];
        const uint8_t bit = 1U << (y & 7);
        if (color == INVERT) value ^= bit;
        else if (color == BLACK) value &= ~bit;
        else value |= bit;
    }
    uint8_t getPixel(int16_t x, int16_t y) const {
        if (x < 0 || y < 0 || x >= WIDTH || y >= HEIGHT) return 0;
        return (sBuffer[(y / 8) * WIDTH + x] >> (y & 7)) & 1;
    }

    // Arduboy2 collision helpers: a point inside a rect, or two overlapping
    // rects. Edges are half-open, matching the upstream implementation.
    static bool collide(CrabitArduboy::Point point, CrabitArduboy::Rect rect) {
        return point.x >= rect.x && point.x < int16_t(rect.x + rect.width) &&
               point.y >= rect.y && point.y < int16_t(rect.y + rect.height);
    }
    static bool collide(CrabitArduboy::Rect a, CrabitArduboy::Rect b) {
        return a.x < int16_t(b.x + b.width) && int16_t(a.x + a.width) > b.x &&
               a.y < int16_t(b.y + b.height) && int16_t(a.y + a.height) > b.y;
    }

    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
        const int32_t right = int32_t(x) + w, bottom = int32_t(y) + h;
        if (w <= 0 || h <= 0 || right <= 0 || bottom <= 0) return;
        for (int yy = y < 0 ? 0 : y; yy < bottom && yy < HEIGHT; ++yy)
            for (int xx = x < 0 ? 0 : x; xx < right && xx < WIDTH; ++xx)
                drawPixel(xx, yy, color);
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint8_t color) {
        fillRect(x, y, w, 1, color);
    }
    void drawFastVLine(int16_t x, int16_t y, int16_t h, uint8_t color) {
        fillRect(x, y, 1, h, color);
    }
    void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t color) {
        int32_t x = x0, y = y0;
        const int32_t dx = labs(int32_t(x1) - x), dy = -labs(int32_t(y1) - y);
        const int sx = x < x1 ? 1 : -1, sy = y < y1 ? 1 : -1;
        int32_t error = dx + dy;
        for (;;) {
            drawPixel(x, y, color);
            if (x == x1 && y == y1) break;
            const int32_t twice = 2 * error;
            if (twice >= dy) { error += dy; x += sx; }
            if (twice <= dx) { error += dx; y += sy; }
        }
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t color) {
        if (w <= 0 || h <= 0) return;
        drawFastHLine(x, y, w, color);
        if (h > 1) drawFastHLine(x, y + h - 1, w, color);
        if (h > 2) {
            drawFastVLine(x, y + 1, h - 2, color);
            if (w > 1) drawFastVLine(x + w - 1, y + 1, h - 2, color);
        }
    }
    void drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color) {
        drawRect(x, y, w, h, color);
    }
    void fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color) {
        fillRect(x, y, w, h, color);
    }
    void drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color) {
        drawFastHLine(x, y, w, color);
    }
    void drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color) {
        drawFastVLine(x, y, h, color);
    }
    void drawCircle(int16_t cx, int16_t cy, uint8_t radius, uint8_t color) {
        int x = radius, y = 0, error = 1 - x;
        while (x >= y) {
            drawPixel(cx + x, cy + y, color);
            if (x) drawPixel(cx - x, cy + y, color);
            if (y) {
                drawPixel(cx + x, cy - y, color);
                if (x) drawPixel(cx - x, cy - y, color);
            }
            if (x != y) {
                drawPixel(cx + y, cy + x, color);
                if (y) drawPixel(cx - y, cy + x, color);
                if (x) drawPixel(cx + y, cy - x, color);
                if (x && y) drawPixel(cx - y, cy - x, color);
            }
            ++y;
            if (error < 0) error += 2 * y + 1;
            else { --x; error += 2 * (y - x) + 1; }
        }
    }
    void fillCircle(int16_t cx, int16_t cy, uint8_t radius, uint8_t color) {
        for (int y = -int(radius); y <= int(radius); ++y)
            for (int x = -int(radius); x <= int(radius); ++x)
                if (x * x + y * y <= int(radius) * int(radius)) drawPixel(cx + x, cy + y, color);
    }
    void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      uint8_t color) {
        drawLine(x0, y0, x1, y1, color);
        drawLine(x1, y1, x2, y2, color);
        drawLine(x2, y2, x0, y0, color);
    }
    void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2,
                      uint8_t color) {
        // Edge-function fill: simple and correct for clipped/degenerate triangles.
        auto edge = [](int32_t ax, int32_t ay, int32_t bx, int32_t by, int32_t px,
                       int32_t py) -> int32_t {
            return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
        };
        const int32_t area = edge(x0, y0, x1, y1, x2, y2);
        if (area == 0) {
            drawLine(x0, y0, x1, y1, color);
            drawLine(x1, y1, x2, y2, color);
            drawLine(x2, y2, x0, y0, color);
            return;
        }
        const int16_t minX = ArduboyMath::min(x0, ArduboyMath::min(x1, x2));
        const int16_t maxX = ArduboyMath::max(x0, ArduboyMath::max(x1, x2));
        const int16_t minY = ArduboyMath::min(y0, ArduboyMath::min(y1, y2));
        const int16_t maxY = ArduboyMath::max(y0, ArduboyMath::max(y1, y2));
        for (int16_t y = minY; y <= maxY; ++y) {
            for (int16_t x = minX; x <= maxX; ++x) {
                const int32_t w0 = edge(x1, y1, x2, y2, x, y);
                const int32_t w1 = edge(x2, y2, x0, y0, x, y);
                const int32_t w2 = edge(x0, y0, x1, y1, x, y);
                if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                    drawPixel(x, y, color);
            }
        }
    }
    void drawRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t r, uint8_t color) {
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;
        drawFastHLine(x + r, y, w - 2 * r, color);
        drawFastHLine(x + r, y + h - 1, w - 2 * r, color);
        drawFastVLine(x, y + r, h - 2 * r, color);
        drawFastVLine(x + w - 1, y + r, h - 2 * r, color);
        for (uint8_t i = 0; i < r; ++i) {
            const int16_t dy = r - i;
            int16_t dx = 0;
            while (dx * dx + dy * dy <= int(r) * int(r)) ++dx;
            if (dx > 0) --dx;
            drawPixel(x + r - dx, y + i, color);
            drawPixel(x + w - 1 - r + dx, y + i, color);
            drawPixel(x + r - dx, y + h - 1 - i, color);
            drawPixel(x + w - 1 - r + dx, y + h - 1 - i, color);
        }
    }
    void fillRoundRect(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t r, uint8_t color) {
        if (r > w / 2) r = w / 2;
        if (r > h / 2) r = h / 2;
        fillRect(x + r, y, w - 2 * r, h, color);
        fillRect(x, y + r, r, h - 2 * r, color);
        fillRect(x + w - r, y + r, r, h - 2 * r, color);
        for (uint8_t i = 0; i < r; ++i) {
            const int16_t dy = r - i;
            int16_t dx = 0;
            while (dx * dx + dy * dy <= int(r) * int(r)) ++dx;
            if (dx > 0) --dx;
            fillRect(x + r - dx, y + i, dx, 1, color);
            fillRect(x + w - 1 - r, y + i, dx + 1, 1, color);
            fillRect(x + r - dx, y + h - 1 - i, dx, 1, color);
            fillRect(x + w - 1 - r, y + h - 1 - i, dx + 1, 1, color);
        }
    }

    void drawBitmap(int16_t x, int16_t y, const uint8_t *image, uint8_t w, uint8_t h,
                    uint8_t color = WHITE) {
        if (!image) return;
        for (int row = 0; row < h; row += 8)
            for (int column = 0; column < w; ++column) {
                const uint8_t bits = pgm_read_byte(image++);
                for (int bit = 0; bit < 8 && row + bit < h; ++bit)
                    if (bits & (1U << bit)) drawPixel(x + column, y + row + bit, color);
            }
    }
    // Arduboy2's compressed format is not implemented. Kept only so ports that
    // reference it fail at runtime rather than at link time; do not use.
    void drawCompressed(int16_t, int16_t, const uint8_t *, uint8_t = WHITE) {}

    int16_t cursor_x = 0, cursor_y = 0;
    uint8_t textSize = 1;
    bool textWrap = true;
    void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
    void setTextSize(uint8_t size) { textSize = size ? size : 1; }
    void setTextWrap(bool wrap) { textWrap = wrap; }
    void setTextColor(uint8_t color) {
        textColor = color;
        textBackground = color == BLACK ? WHITE : BLACK;
    }
    void setTextColor(uint8_t color, uint8_t background) {
        textColor = color;
        textBackground = background;
    }
    uint8_t getTextColor() const { return textColor; }
    size_t write(uint8_t c) {
        if (c == '\r') return 1;
        if (c == '\n') { cursor_x = 0; cursor_y += 8 * textSize; return 1; }
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c < ' ' || c > '~') return 1;
        drawChar(cursor_x, cursor_y, c, textColor, textBackground, textSize);
        cursor_x += 6 * textSize;
        if (textWrap && cursor_x > WIDTH - 6 * textSize) { cursor_x = 0; cursor_y += 8 * textSize; }
        return 1;
    }
    size_t print(const char *text) {
        size_t n = 0;
        if (text) while (*text) n += write(static_cast<uint8_t>(*text++));
        return n;
    }
    size_t print(const __FlashStringHelper *text) {
        return print(reinterpret_cast<const char *>(text));
    }
    size_t print(unsigned long value) {
        char buffer[3 * sizeof(value) + 1];
        char *p = buffer + sizeof(buffer);
        *--p = 0;
        do { *--p = char('0' + value % 10); value /= 10; } while (value);
        return print(p);
    }
    size_t print(long value) {
        if (value >= 0) return print(static_cast<unsigned long>(value));
        return write('-') + print(0UL - static_cast<unsigned long>(value));
    }
    size_t print(int value) { return print(static_cast<long>(value)); }
    size_t print(unsigned int value) { return print(static_cast<unsigned long>(value)); }
    size_t print(char value) { return write(static_cast<uint8_t>(value)); }
    size_t printEx(int16_t x, int16_t y, const char *text) { setCursor(x, y); return print(text); }
    size_t printEx(int16_t x, int16_t y, const __FlashStringHelper *text) {
        setCursor(x, y);
        return print(text);
    }
    void drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t background,
                  uint8_t size = 1) {
        if (c < ' ' || c > '~') return;
        const uint8_t *glyph = arduboyFont5x7() + (c - ' ') * 5;
        const bool drawBackground = background != color;
        for (uint8_t i = 0; i < 5; ++i) {
            const uint8_t line = pgm_read_byte(glyph + i);
            for (uint8_t j = 0; j < 7; ++j) {
                const bool on = (line >> j) & 1;
                if (on) fillRect(x + i * size, y + j * size, size, size, color);
                else if (drawBackground) fillRect(x + i * size, y + j * size, size, size, background);
            }
        }
    }

    uint32_t generateRandomSeed() {
        return uint32_t(micros()) ^ (uint32_t(millis()) << 16) ^ uint32_t(::rand());
    }
    void initRandomSeed() { randomSeed(generateRandomSeed()); }
    void setRGBled(uint8_t, uint8_t, uint8_t) {}
    void digitalWriteRGB(uint8_t, uint8_t, uint8_t) {}
    bool isAudioEnabled() const { return CrabitArduboyAudio::enabled(); }
    void setAudioEnabled(bool on) { CrabitArduboyAudio::setEnabled(on); }
    void saveAudioOnOff() {}
    // Base entry points used by ports that do not define their own helper.
    // playScore2 keeps the pinned upstream MyArduboy contract: priority is the
    // raw uint8_t scale where a LOWER value is more important, shared with the
    // port's playTone(priority). It must not be fed into the compat ladder.
    void playScore2(const uint8_t *score, uint8_t priority = 0, int8_t pitch = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority, pitch);
    }
    void stopScore2() { CrabitArduboyAudio::stop(); }
    void tone2(uint16_t frequency, uint16_t durationMs) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playTone(frequency, durationMs,
                                         CrabitArduboyAudio::PriorityEffect);
    }

private:
    Canvas *_canvas = nullptr;
    uint8_t hardware = 0, current = 0, previous = 0, rate = 60;
    uint8_t textColor = WHITE, textBackground = BLACK;
    uint32_t phase = 0, lastTime = 0;
    uint32_t polledFrame = 0;
    bool started = false;
    bool polled = false;

};

// Printable ASCII 32..127, 5x7 columns, LSB at the top. Source: the
// project's standard NanoUI 5x7 font (libraries/NanoUI/src/NanoUI/product/
// core/default_font.h), restricted to the printable range.
static const uint8_t *arduboyFont5x7() {
    static const uint8_t font[480] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F, 0x00, 0x00, 0x00, 0x07, 0x00, 0x07, 0x00,
    0x14, 0x7F, 0x14, 0x7F, 0x14, 0x24, 0x2A, 0x7F, 0x2A, 0x12, 0x23, 0x13, 0x08, 0x64, 0x62,
    0x36, 0x49, 0x56, 0x20, 0x50, 0x00, 0x08, 0x07, 0x03, 0x00, 0x00, 0x1C, 0x22, 0x41, 0x00,
    0x00, 0x41, 0x22, 0x1C, 0x00, 0x2A, 0x1C, 0x7F, 0x1C, 0x2A, 0x08, 0x08, 0x3E, 0x08, 0x08,
    0x00, 0x80, 0x70, 0x30, 0x00, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00, 0x60, 0x60, 0x00,
    0x20, 0x10, 0x08, 0x04, 0x02, 0x3E, 0x51, 0x49, 0x45, 0x3E, 0x00, 0x42, 0x7F, 0x40, 0x00,
    0x72, 0x49, 0x49, 0x49, 0x46, 0x21, 0x41, 0x49, 0x4D, 0x33, 0x18, 0x14, 0x12, 0x7F, 0x10,
    0x27, 0x45, 0x45, 0x45, 0x39, 0x3C, 0x4A, 0x49, 0x49, 0x31, 0x41, 0x21, 0x11, 0x09, 0x07,
    0x36, 0x49, 0x49, 0x49, 0x36, 0x46, 0x49, 0x49, 0x29, 0x1E, 0x00, 0x00, 0x14, 0x00, 0x00,
    0x00, 0x40, 0x34, 0x00, 0x00, 0x00, 0x08, 0x14, 0x22, 0x41, 0x14, 0x14, 0x14, 0x14, 0x14,
    0x00, 0x41, 0x22, 0x14, 0x08, 0x02, 0x01, 0x59, 0x09, 0x06, 0x3E, 0x41, 0x5D, 0x59, 0x4E,
    0x7C, 0x12, 0x11, 0x12, 0x7C, 0x7F, 0x49, 0x49, 0x49, 0x36, 0x3E, 0x41, 0x41, 0x41, 0x22,
    0x7F, 0x41, 0x41, 0x41, 0x3E, 0x7F, 0x49, 0x49, 0x49, 0x41, 0x7F, 0x09, 0x09, 0x09, 0x01,
    0x3E, 0x41, 0x41, 0x51, 0x73, 0x7F, 0x08, 0x08, 0x08, 0x7F, 0x00, 0x41, 0x7F, 0x41, 0x00,
    0x20, 0x40, 0x41, 0x3F, 0x01, 0x7F, 0x08, 0x14, 0x22, 0x41, 0x7F, 0x40, 0x40, 0x40, 0x40,
    0x7F, 0x02, 0x1C, 0x02, 0x7F, 0x7F, 0x04, 0x08, 0x10, 0x7F, 0x3E, 0x41, 0x41, 0x41, 0x3E,
    0x7F, 0x09, 0x09, 0x09, 0x06, 0x3E, 0x41, 0x51, 0x21, 0x5E, 0x7F, 0x09, 0x19, 0x29, 0x46,
    0x26, 0x49, 0x49, 0x49, 0x32, 0x03, 0x01, 0x7F, 0x01, 0x03, 0x3F, 0x40, 0x40, 0x40, 0x3F,
    0x1F, 0x20, 0x40, 0x20, 0x1F, 0x3F, 0x40, 0x38, 0x40, 0x3F, 0x63, 0x14, 0x08, 0x14, 0x63,
    0x03, 0x04, 0x78, 0x04, 0x03, 0x61, 0x59, 0x49, 0x4D, 0x43, 0x00, 0x7F, 0x41, 0x41, 0x41,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x00, 0x41, 0x41, 0x41, 0x7F, 0x04, 0x02, 0x01, 0x02, 0x04,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x03, 0x07, 0x08, 0x00, 0x20, 0x54, 0x54, 0x78, 0x40,
    0x7F, 0x28, 0x44, 0x44, 0x38, 0x38, 0x44, 0x44, 0x44, 0x28, 0x38, 0x44, 0x44, 0x28, 0x7F,
    0x38, 0x54, 0x54, 0x54, 0x18, 0x00, 0x08, 0x7E, 0x09, 0x02, 0x18, 0xA4, 0xA4, 0x9C, 0x78,
    0x7F, 0x08, 0x04, 0x04, 0x78, 0x00, 0x44, 0x7D, 0x40, 0x00, 0x20, 0x40, 0x40, 0x3D, 0x00,
    0x7F, 0x10, 0x28, 0x44, 0x00, 0x00, 0x41, 0x7F, 0x40, 0x00, 0x7C, 0x04, 0x78, 0x04, 0x78,
    0x7C, 0x08, 0x04, 0x04, 0x78, 0x38, 0x44, 0x44, 0x44, 0x38, 0xFC, 0x18, 0x24, 0x24, 0x18,
    0x18, 0x24, 0x24, 0x18, 0xFC, 0x7C, 0x08, 0x04, 0x04, 0x08, 0x48, 0x54, 0x54, 0x54, 0x24,
    0x04, 0x04, 0x3F, 0x44, 0x24, 0x3C, 0x40, 0x40, 0x20, 0x7C, 0x1C, 0x20, 0x40, 0x20, 0x1C,
    0x3C, 0x40, 0x30, 0x40, 0x3C, 0x44, 0x28, 0x10, 0x28, 0x44, 0x4C, 0x90, 0x90, 0x90, 0x7C,
    0x44, 0x64, 0x54, 0x4C, 0x44, 0x00, 0x08, 0x36, 0x41, 0x00, 0x00, 0x00, 0x77, 0x00, 0x00,
    0x00, 0x41, 0x36, 0x08, 0x00, 0x02, 0x01, 0x02, 0x04, 0x02, 0x3C, 0x26, 0x23, 0x26, 0x3C,
    };
    return font;
}

class SpritesCompat {
public:
    explicit SpritesCompat(Arduboy2Compat &engine) : _ab(&engine) {}
    SpritesCompat() : _ab(g_arduboyCompatActive) {}
    void setEngine(Arduboy2Compat &engine) { _ab = &engine; }

    void drawOverwrite(int16_t x, int16_t y, const uint8_t *image, uint8_t frame) {
        draw(x, y, image, frame, WHITE, false);
    }
    void drawSelfMasked(int16_t x, int16_t y, const uint8_t *image, uint8_t frame) {
        draw(x, y, image, frame, WHITE, true);
    }
    void drawErase(int16_t x, int16_t y, const uint8_t *image, uint8_t frame) {
        draw(x, y, image, frame, BLACK, true);
    }
    void drawPlusMask(int16_t x, int16_t y, const uint8_t *image, uint8_t frame) {
        if (!_ab || !image) return;
        const uint8_t w = pgm_read_byte(image), h = pgm_read_byte(image + 1);
        const int bytesPerFrame = w * ((h + 7) / 8) * 2;
        const uint8_t *data = image + 2 + frame * bytesPerFrame;
        for (int row = 0; row < h; row += 8)
            for (int column = 0; column < w; ++column) {
                const uint8_t pixels = pgm_read_byte(data++);
                const uint8_t mask = pgm_read_byte(data++);
                for (int bit = 0; bit < 8 && row + bit < h; ++bit)
                    if (mask & (1U << bit))
                        _ab->drawPixel(x + column, y + row + bit,
                                       (pixels & (1U << bit)) ? WHITE : BLACK);
            }
    }
    void drawExternalMask(int16_t x, int16_t y, const uint8_t *image, const uint8_t *mask,
                          uint8_t frame, uint8_t maskFrame) {
        if (!_ab || !image || !mask) return;
        const uint8_t w = pgm_read_byte(image), h = pgm_read_byte(image + 1);
        const int bytesPerFrame = w * ((h + 7) / 8);
        const uint8_t *data = image + 2 + frame * bytesPerFrame;
        const uint8_t *maskData = mask + 2 + maskFrame * bytesPerFrame;
        for (int row = 0; row < h; row += 8)
            for (int column = 0; column < w; ++column) {
                const uint8_t pixels = pgm_read_byte(data++);
                const uint8_t maskBits = pgm_read_byte(maskData++);
                for (int bit = 0; bit < 8 && row + bit < h; ++bit)
                    if (maskBits & (1U << bit))
                        _ab->drawPixel(x + column, y + row + bit,
                                       (pixels & (1U << bit)) ? WHITE : BLACK);
            }
    }
    // Compatibility alias used by some ports.
    void drawBitmap(int16_t x, int16_t y, const uint8_t *image, uint8_t frame) {
        drawSelfMasked(x, y, image, frame);
    }

private:
    Arduboy2Compat *_ab;
    void draw(int16_t x, int16_t y, const uint8_t *image, uint8_t frame, uint8_t color,
              bool mask) {
        if (!_ab || !image) return;
        const uint8_t w = pgm_read_byte(image), h = pgm_read_byte(image + 1);
        const int bytesPerFrame = w * ((h + 7) / 8);
        const uint8_t *data = image + 2 + frame * bytesPerFrame;
        for (int row = 0; row < h; row += 8)
            for (int column = 0; column < w; ++column) {
                const uint8_t pixels = pgm_read_byte(data++);
                for (int bit = 0; bit < 8 && row + bit < h; ++bit) {
                    const bool on = pixels & (1U << bit);
                    if (on || !mask) _ab->drawPixel(x + column, y + row + bit, on ? color : BLACK);
                }
            }
    }
};

// ArduboyTones-compatible surface. Playback is delegated to the one shared
// mono adapter; there is no AVR timer, speaker pin or per-instance engine.
class ArduboyTonesCompat {
public:
    explicit ArduboyTonesCompat(bool (*outputEnabled)() = nullptr)
        : _outputEnabled(outputEnabled) {}
    // Some ports construct this with an unrelated helper type.
    template<class T> explicit ArduboyTonesCompat(T) : _outputEnabled(nullptr) {}

    void tone(uint16_t frequency, uint16_t duration = 0) {
        if (gate()) CrabitArduboyAudio::playTone(frequency, duration,
                                                 CrabitArduboyAudio::PriorityEffect);
    }
    void tone(uint16_t frequency1, uint16_t duration1,
              uint16_t frequency2, uint16_t duration2) {
        const uint16_t pairs[4] = {frequency1, duration1, frequency2, duration2};
        if (gate()) CrabitArduboyAudio::playTonePairs(
            pairs, 2, CrabitArduboyAudio::PriorityEffect);
    }
    void tone(uint16_t frequency1, uint16_t duration1,
              uint16_t frequency2, uint16_t duration2,
              uint16_t frequency3, uint16_t duration3) {
        const uint16_t pairs[6] = {frequency1, duration1, frequency2, duration2,
                                   frequency3, duration3};
        if (gate()) CrabitArduboyAudio::playTonePairs(
            pairs, 3, CrabitArduboyAudio::PriorityEffect);
    }
    void tones(const uint16_t *pairs) {
        if (gate()) CrabitArduboyAudio::playTones(pairs,
                                                  CrabitArduboyAudio::TerminatorHigh,
                                                  CrabitArduboyAudio::PrioritySequence);
    }
    void tonesInRAM(uint16_t *pairs) { tones(pairs); }
    void noTone() { CrabitArduboyAudio::stop(); }
    bool playing() const { return CrabitArduboyAudio::playing(); }
    void volume(uint8_t) {}     // No PWM volume control.
    void volumeMode(uint8_t) {} // Volume bits are stripped by the adapter.
private:
    bool gate() const { return !_outputEnabled || _outputEnabled(); }
    bool (*_outputEnabled)();
};

// ArduboyPlaytune-compatible surface. No timer or pin setup; scores are parsed
// by the bounded mono adapter on the existing App tick.
class ArduboyPlaytuneCompat {
public:
    explicit ArduboyPlaytuneCompat(bool (*outputEnabled)() = nullptr)
        : _outputEnabled(outputEnabled) {}
    void initChannel(uint8_t) {}
    void playScore(const uint8_t *score) {
        if (gate()) CrabitArduboyAudio::playPlaytune(score,
                                                     CrabitArduboyAudio::PriorityScore);
    }
    void playScore(const uint8_t *score, uint16_t) { playScore(score); }
    void stop() { CrabitArduboyAudio::stop(); }
    void tone(uint16_t frequency, uint16_t duration) {
        if (gate()) CrabitArduboyAudio::playTone(frequency, duration,
                                                 CrabitArduboyAudio::PriorityEffect);
    }
    void noTone() { CrabitArduboyAudio::stop(); }
    void setVolume(uint8_t) {}
private:
    bool gate() const { return !_outputEnabled || _outputEnabled(); }
    bool (*_outputEnabled)();
};

// Short names used by upstream sources inside their own game namespace.
using Arduboy2CompatTones = ArduboyTonesCompat;

#endif
