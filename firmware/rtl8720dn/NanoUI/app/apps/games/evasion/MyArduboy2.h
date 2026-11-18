#ifndef EVASION_MYARDBUOY2_H
#define EVASION_MYARDBUOY2_H

// Evasion's BW16 adaptation of OBONO's local Arduboy2 helper. The AVR
// timer-based Playtune implementation is replaced by silent compatibility
// stubs; all drawing, input edges and frame pacing use the shared engine.
class MyArduboy2 : public Arduboy2Compat {
public:
    void beginNoLogo();
    bool nextFrame();
    bool buttonDown(uint8_t buttons);
    bool buttonPressed(uint8_t buttons);
    bool buttonUp(uint8_t buttons);
    void setTextColors(uint8_t color, uint8_t bg) { setTextColor(color, bg); }
    template <typename T> size_t printEx(int16_t x, int16_t y, T value) {
        setCursor(x, y);
        return print(value);
    }

    void initAudio(uint8_t) {}
    void closeAudio() {}
    bool isAudioEnabled() const { return Arduboy2Compat::isAudioEnabled(); }
    void setAudioEnabled(bool on) { Arduboy2Compat::setAudioEnabled(on); }
    void toggleAudioEnabled() { setAudioEnabled(!isAudioEnabled()); }
    void saveAudioOnOff() {}
    // Register upstream scores/effects on the shared mono adapter. Playback and
    // the wire request run on ArduboyCompatApp's bounded audio tick.
    void playScore(const byte *score, uint8_t priority = 0, int8_t pitch = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority, pitch);
    }
    void stopScore() { CrabitArduboyAudio::stop(); }
    void playTone(uint16_t frequency, uint16_t durationMs,
                  uint8_t priority = 0xFF, uint8_t = 2) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playToneRaw(frequency, durationMs, priority);
    }

private:
    uint8_t lastButtonState = 0, currentButtonState = 0;
};

#endif // EVASION_MYARDBUOY2_H
