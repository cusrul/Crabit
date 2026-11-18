#pragma once

// Galaxion used the first-generation Arduboy library. Keep its object and
// method names, backed by the shared buffer-only compatibility engine.
class Arduboy : public Arduboy2Compat {
public:
    struct Tunes {
        void tone(uint16_t frequency, uint16_t durationMs) {
            if (CrabitArduboyAudio::enabled())
                CrabitArduboyAudio::playTone(frequency, durationMs,
                                             CrabitArduboyAudio::PriorityEffect);
        }
        void playScore(const uint8_t *score) {
            if (CrabitArduboyAudio::enabled())
                CrabitArduboyAudio::playPlaytune(score,
                                                 CrabitArduboyAudio::PriorityScore);
        }
        void stopScore() { CrabitArduboyAudio::stop(); }
        bool playing() const { return CrabitArduboyAudio::playing(); }
    } tunes;
};
