#pragma once

// OBONO's Chri-Bocchi Cat helpers adapted to the buffer-only BW16 port engine.
// The upstream class inherited the AVR Arduboy/Print implementation; this
// version keeps its compact 6x6 font, clipping helpers and button edges on top
// of the shared Arduboy2Compat framebuffer. Audio and RGB calls fall through to
// the shared silent stubs.
typedef uint8_t buffer_t;
class MyArduboy : public Arduboy2Compat {
public:
    void beginNoLogo() {
        clear();
        cursor_x = cursor_y = 0;
        textSize = 1;
        textWrap = false;
        textcolor = WHITE;
        textbg = BLACK;
        lastButtonState = currentButtonState = 0;
    }
    bool nextFrame();
    bool buttonDown(uint8_t buttons);
    bool buttonPressed(uint8_t buttons);
    bool buttonUp(uint8_t buttons);
    void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
    void setTextColor(uint8_t color);
    void setTextColor(uint8_t color, uint8_t bg);
    size_t printEx(int16_t x, int16_t y, const char *p);
    size_t printEx(int16_t x, int16_t y, const __FlashStringHelper *p);
    size_t print(const char *p) { size_t n = 0; while (*p) n += write(static_cast<uint8_t>(*p++)); return n; }
    size_t print(char *p) { return print(static_cast<const char *>(p)); }
    size_t print(const __FlashStringHelper *p) { return print(reinterpret_cast<const char *>(p)); }
    size_t print(char c) { return write(static_cast<uint8_t>(c)); }
    template<typename T> size_t print(T value) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(value));
        return print(buf);
    }
    size_t write(uint8_t c);
    void drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color);
    void drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color);
    void fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
private:
    void myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);
    void fillBeltBlack(buffer_t *p, uint8_t d, uint8_t w);
    void fillBeltWhite(buffer_t *p, uint8_t d, uint8_t w);
    int16_t cursor_x = 0, cursor_y = 0;
    uint8_t textSize = 1;
    bool textWrap = false;
    uint8_t textcolor = WHITE, textbg = BLACK;
    uint8_t lastButtonState = 0, currentButtonState = 0;
};
