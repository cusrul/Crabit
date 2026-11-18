#pragma once

// Space Tanks was written for the first-generation Arduboy library. This
// adapter keeps the same object name and methods while using the shared
// buffer-only compatibility engine.
typedef bool boolean;

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
