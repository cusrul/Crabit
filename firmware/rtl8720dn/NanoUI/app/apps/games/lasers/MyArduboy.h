#pragma once

// BW16 adaptation of OBONO's local Arduboy helper. No AVR/audio hardware.
typedef uint8_t buffer_t;
class MyArduboy : public Arduboy2Compat {
public:
    void beginNoLogo();
    bool nextFrame();
    bool buttonDown(uint8_t b);
    bool buttonPressed(uint8_t b);
    bool buttonUp(uint8_t b);
    void setCursor(int16_t x, int16_t y) { cursor_x = x; cursor_y = y; }
    void setTextColor(uint8_t color);
    void setTextColor(uint8_t color, uint8_t bg);
    size_t print(const char* p) { size_t n = 0; while (*p) n += write(*p++); return n; }
    size_t print(const __FlashStringHelper* p) { return print(reinterpret_cast<const char*>(p)); }
    size_t print(char c) { return write(c); }
    template<class T> size_t print(T v) { char b[24]; snprintf(b, sizeof(b), "%ld", static_cast<long>(v)); return print(static_cast<const char*>(b)); }
    size_t printEx(int16_t x, int16_t y, const char* p);
    size_t printEx(int16_t x, int16_t y, const __FlashStringHelper* p);
    size_t write(uint8_t c);
    void drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color);
    void drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color);
    void fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void drawFastVLine(int16_t x, int16_t y, uint8_t h, uint8_t c) { drawFastVLine2(x,y,h,c); }
    void drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t c) { drawFastHLine2(x,y,w,c); }
    void setRGBled(uint8_t, uint8_t, uint8_t) {}
    // Audio entry points come from the shared Arduboy2Compat adapter.
private:
    void myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);
    void fillBeltBlack(buffer_t* p, uint8_t d, uint8_t w);
    void fillBeltWhite(buffer_t* p, uint8_t d, uint8_t w);
    int16_t cursor_x = 0, cursor_y = 0;
    uint8_t textSize = 1;
    bool textWrap = true;
    uint8_t textcolor = WHITE, textbg = BLACK;
    uint8_t lastButtonState = 0, currentButtonState = 0;
};
