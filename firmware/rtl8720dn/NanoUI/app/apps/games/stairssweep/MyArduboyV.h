#pragma once

// Stairs Sweep's old-Arduboy helper, adapted to the shared BW16 engine.
typedef uint8_t buffer_t;

class MyArduboyV : public Arduboy2Compat {
public:
    void    beginNoLogo(void);
    bool    nextFrame(void);
    bool    buttonDown(uint8_t buttons);
    bool    buttonPressed(uint8_t buttons);
    bool    buttonUp(uint8_t buttons);
    void    setTextColor(uint8_t color);
    void    setTextColor(uint8_t color, uint8_t bg);
    using Arduboy2Compat::write;
    size_t  write(uint8_t c);
    template <typename T> size_t printEx(int16_t x, int16_t y, T p) {
        setCursor(x, y);
        return print(p);
    }
    size_t  print(const char *p) { size_t n = 0; while (p && *p) n += write(*p++); return n; }
    size_t  print(char *p) { return print(static_cast<const char *>(p)); }
    size_t  print(const __FlashStringHelper *p) {
        return print(reinterpret_cast<const char *>(p));
    }
    size_t  print(char c) { return write(static_cast<uint8_t>(c)); }
    template <typename T> size_t print(T value) {
        char buffer[24];
        snprintf(buffer, sizeof(buffer), "%ld", static_cast<long>(value));
        return print(buffer);
    }
    void    drawRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    void    drawFastVLine2(int16_t x, int16_t y, int8_t h, uint8_t color);
    void    drawFastHLine2(int16_t x, int16_t y, uint8_t w, uint8_t color);
    void    fillRect2(int16_t x, int16_t y, uint8_t w, int8_t h, uint8_t color);
    bool    isAudioEnabled(void) { return Arduboy2Compat::isAudioEnabled(); }
    void    setAudioEnabled(bool on) { Arduboy2Compat::setAudioEnabled(on); }
    void    saveAudioOnOff(void) {}
    void    initAudio(uint8_t) {}
    void    closeAudio(void) {}
    void    playTone(uint16_t frequency, uint16_t durationMs,
                     uint8_t priority = 0xFF, uint8_t = 2) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playToneRaw(frequency, durationMs, priority);
    }
    void    playScore2(const byte *score, uint8_t priority = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority);
    }
    void    playScore(const byte *score, uint8_t priority = 0, int8_t pitch = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority, pitch);
    }
    void    stopScore2(void) { CrabitArduboyAudio::stop(); }
    void    stopScore(void) { CrabitArduboyAudio::stop(); }

private:
    void    myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color,
                       uint8_t bg, uint8_t size);
    void    fillBeltBlack(buffer_t *p, uint8_t d, uint8_t w);
    void    fillBeltWhite(buffer_t *p, uint8_t d, uint8_t w);
    uint8_t textcolor = WHITE;
    uint8_t textbg = BLACK;
    uint8_t lastButtonState = 0;
    uint8_t currentButtonState = 0;
};
