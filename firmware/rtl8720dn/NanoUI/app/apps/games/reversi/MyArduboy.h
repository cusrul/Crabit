#ifndef REVERSI_MYARDBUOY_H
#define REVERSI_MYARDBUOY_H

// OBONO's Chie Magari Ita helpers adapted to the buffer-only BW16 port engine.
// Upstream inherits the Arduboy 1.1.1 class; this keeps only the parts the game
// uses and implements them on top of the shared Arduboy2Compat framebuffer.
typedef uint8_t buffer_t;

// Arduboy 1.1.1 reserves EEPROM address 2 for the audio on/off flag. The port
// keeps the same address inside the engine's private RAM-only EEPROM, so the
// game's SOUND menu item survives app reentry but never power loss.
#define EEPROM_AUDIO_ON_OFF 2
#define EEPROM_STORAGE_SPACE_START 16

class MyArduboy : public Arduboy2Compat {
public:
    // Replaces the engine's silent stub: the menu must still be able to show
    // and remember ON/OFF even though there is no speaker or score playback.
    struct PortAudio {
        Arduboy2Compat::RamEeprom *storage = nullptr;
        bool onFlag = false;
        void begin() {
            onFlag = storage ? storage->read(EEPROM_AUDIO_ON_OFF) != 0 : false;
            CrabitArduboyAudio::setEnabled(onFlag);
        }
        bool enabled() const { return onFlag; }
        void on() { onFlag = true; CrabitArduboyAudio::setEnabled(true); }
        void off() { onFlag = false; CrabitArduboyAudio::setEnabled(false); }
        void toggle() { if (onFlag) off(); else on(); }
        void saveOnOff() {
            if (storage) storage->update(EEPROM_AUDIO_ON_OFF, onFlag ? 1 : 0);
        }
    } audio;

    MyArduboy() { audio.storage = &eeprom; }

    void beginNoLogo() {
        clear();
        cursor_x = cursor_y = 0;
        textSize = 1;
        textWrap = false;
        textcolor = WHITE;
        textbg = BLACK;
        lastButtonState = currentButtonState = 0;
        audio.begin();
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
    size_t print(const char *p) { size_t n = 0; while (*p) n += write(*p++); return n; }
    size_t print(char *p) { return print(static_cast<const char *>(p)); }
    size_t print(const __FlashStringHelper *p) { return print(reinterpret_cast<const char *>(p)); }
    size_t print(char c) { return write(c); }
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
    bool isAudioEnabled() { return audio.enabled(); }
    void setAudioEnabled(bool on) { if (on) audio.on(); else audio.off(); }
    void saveAudioOnOff() { audio.saveOnOff(); }
    // Register the retained upstream score on the shared mono adapter.
    void playScore2(const byte *score, uint8_t priority = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority);
    }
    void stopScore2() { CrabitArduboyAudio::stop(); }
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

#endif // REVERSI_MYARDBUOY_H
