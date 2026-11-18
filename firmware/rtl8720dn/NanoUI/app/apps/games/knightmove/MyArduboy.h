#pragma once

// BW16 adaptation of OBONO's Arduboy 1.1 helper. Pacing, input edges, drawing
// and text come from the shared compatibility engine; audio is a silent stub
// with a RAM-only on/off flag used by the upstream menu.
class MyArduboy : public Arduboy2Compat {
public:
    void beginNoLogo();
    bool nextFrame();
    bool buttonDown(uint8_t buttons);
    bool buttonPressed(uint8_t buttons);
    bool buttonUp(uint8_t buttons);
    bool isAudioEnabled() const { return Arduboy2Compat::isAudioEnabled(); }
    void setAudioEnabled(bool on) { Arduboy2Compat::setAudioEnabled(on); }
    void saveAudioOnOff() {}
    void playScore2(const byte *score, uint8_t priority = 0) {
        if (isAudioEnabled())
            CrabitArduboyAudio::playPlaytuneRaw(score, priority);
    }
    void stopScore2() { CrabitArduboyAudio::stop(); }

private:
    uint8_t lastButtonState = 0, currentButtonState = 0;
};
