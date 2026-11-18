#pragma once

// OBONO's game-local graphics/font API, adapted to the Crabit RAM framebuffer.
typedef uint8_t buffer_t;
class MyArduboy : public Arduboy2Compat {
public:
    void beginNoLogo();
    bool nextFrame();
    bool buttonDown(uint8_t buttons);
    bool buttonPressed(uint8_t buttons);
    bool buttonUp(uint8_t buttons);
    void setTextColor(uint8_t color);
    void setTextColor(uint8_t color, uint8_t bg);
    void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
    size_t print(const char *p) { size_t n = 0; while (*p) n += write(*p++); return n; }
    size_t print(const __FlashStringHelper *p) { return print(reinterpret_cast<const char *>(p)); }
    size_t print(unsigned long value) { char buf[24]; snprintf(buf, sizeof(buf), "%lu", value); return print(buf); }
    size_t print(unsigned int value) { return print(static_cast<unsigned long>(value)); }
    size_t print(int value) { char buf[24]; snprintf(buf, sizeof(buf), "%d", value); return print(buf); }
    size_t printEx(int16_t x, int16_t y, const char *p);
    size_t printEx(int16_t x, int16_t y, const __FlashStringHelper *p);
    size_t write(uint8_t c);
    void drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color);
    void drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color);
    void fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    // Audio entry points come from the shared Arduboy2Compat adapter.
    void setRGBled(uint8_t, uint8_t, uint8_t) {}
private:
    void myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);
    void fillBeltBlack(buffer_t *p, uint8_t d, uint8_t w);
    void fillBeltWhite(buffer_t *p, uint8_t d, uint8_t w);
    int16_t cursor_x = 0, cursor_y = 0;
    uint8_t textSize = 1;
    bool textWrap = true;
    uint8_t textcolor = WHITE, textbg = BLACK;
    uint8_t lastButtonState = 0, currentButtonState = 0;
};
