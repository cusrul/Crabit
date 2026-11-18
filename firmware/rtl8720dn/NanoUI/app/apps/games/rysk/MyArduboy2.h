#pragma once

// RYSK's original MyArduboy2 helpers on the shared Arduboy2Compat engine.
// Audio/Playtune calls are retained as silent no-ops; drawing and input follow
// the upstream implementation.
typedef uint8_t buffer_t;

class MyArduboy2 : public Arduboy2Compat {
public:
    void beginNoLogo() {
        clear();
        cursor_x = cursor_y = 0;
        textSize = 1;
        textWrap = false;
        textcolor = WHITE;
        textbg = BLACK;
        lastButtonState = currentButtonState = 0;
        setAudioEnabled(true);
    }
    bool nextFrame();
    bool buttonDown(uint8_t buttons);
    bool buttonPressed(uint8_t buttons);
    bool buttonUp(uint8_t buttons);
    void setTextColors(uint8_t color, uint8_t bg);
    size_t write(uint8_t c);
    template <typename T> size_t printEx(int16_t x, int16_t y, T p) {
        setCursor(x, y);
        return print(p);
    }
    size_t print(const char *p) { size_t n = 0; while (*p) n += write(*p++); return n; }
    size_t print(char *p) { return print(static_cast<const char *>(p)); }
    size_t print(const __FlashStringHelper *p) {
        return print(reinterpret_cast<const char *>(p));
    }
    size_t print(char c) { return write(c); }
    template <typename T> size_t print(T value) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%ld", static_cast<long>(value));
        return print(buf);
    }
    void drawRect(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void drawFastVLine(int16_t x, int16_t y, int8_t h, uint8_t color);
    void drawFastHLine(int16_t x, int16_t y, uint8_t w, uint8_t color);
    void fillRect(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);

    void initAudio(uint8_t) {}
    void closeAudio() {}
    bool isAudioEnabled() { return Arduboy2Compat::isAudioEnabled(); }
    void setAudioEnabled(bool on) { Arduboy2Compat::setAudioEnabled(on); }
    void toggleAudioEnabled() { setAudioEnabled(!isAudioEnabled()); }
    void saveAudioOnOff() {}
    void playTone(uint16_t frequency, uint16_t durationMs,
                  uint8_t priority = 0xFF, uint8_t = 2) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playToneRaw(frequency, durationMs, priority);
    }
    void playScore(const byte *score, uint8_t priority = 0, int8_t pitch = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority, pitch);
    }
    void stopScore() { CrabitArduboyAudio::stop(); }

private:
    void drawChar(int16_t x, int16_t y, unsigned char c, uint8_t color, uint8_t bg, uint8_t size);
    void fillBeltBlack(uint8_t *p, uint8_t d, uint8_t w);
    void fillBeltWhite(uint8_t *p, uint8_t d, uint8_t w);
    uint8_t lastButtonState = 0, currentButtonState = 0;
    uint8_t textcolor = WHITE, textbg = BLACK;
};
