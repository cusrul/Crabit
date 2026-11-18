#pragma once

// Toyokumono's Arduboy2 helper, adapted to the buffer-only BW16 port engine.
// The original 6x6 compact font and text metrics stay; the shared
// compatibility layer supplies the framebuffer, buttons, RGB LED and silent
// audio. No AVR timer or Playtune channel is initialised.
class MyArduboy2 : public Arduboy2Compat {
public:
    void    beginNoLogo(void);
    bool    nextFrame(void);
    bool    buttonDown(uint8_t buttons);
    bool    buttonPressed(uint8_t buttons);
    bool    buttonUp(uint8_t buttons);
    void    setTextColors(uint8_t color, uint8_t bg);
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

    // Register the original score/tone call sites on the shared mono adapter.
    void    initAudio(uint8_t) {}
    void    closeAudio(void) {}
    void    toggleAudioEnabled(void) { setAudioEnabled(!isAudioEnabled()); }
    void    saveAudioOnOff(void) {}
    void    playTone(uint16_t frequency, uint16_t durationMs,
                     uint8_t priority = 0xFF, uint8_t = 2) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playToneRaw(frequency, durationMs, priority);
    }
    void    playScore(const byte *score, uint8_t priority = 0, int8_t pitch = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority, pitch);
    }
    void    stopScore(void) { CrabitArduboyAudio::stop(); }

private:
    void    myDrawChar(int16_t x, int16_t y, unsigned char c, uint8_t color,
                       uint8_t bg, uint8_t size);
    uint8_t lastButtonState = 0;
    uint8_t currentButtonState = 0;
    uint8_t textcolor = WHITE;
    uint8_t textbg = BLACK;
};
