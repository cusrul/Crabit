#ifndef HOPPER_MYARDBUOY2_H
#define HOPPER_MYARDBUOY2_H

// Hopper's local Arduboy helper, adapted to the shared CrabitArduboy compat
// engine. Drawing, input edges, frame pacing, the 1 KiB RAM EEPROM and the
// mono audio flag come from Arduboy2Compat. `tunes` keeps the upstream
// stopScore() call site on the shared mono adapter.
//
// Upstream's sound menu toggles the compat audio flag; playback is registered
// here and emitted by ArduboyCompatApp's bounded audio tick.
//
// Drawing and the title menu are unchanged.

#include "../../../compat/arduboy/Arduboy2Compat.h"

class MyArduboy2 : public Arduboy2Compat {
public:
    struct HopperTunes {
        void playScore(const uint8_t *score) {
            if (CrabitArduboyAudio::enabled())
                CrabitArduboyAudio::playPlaytune(score,
                                                 CrabitArduboyAudio::PriorityScore);
        }
        void playScore(const uint8_t *score, uint16_t) { playScore(score); }
        void stopScore() { CrabitArduboyAudio::stop(); }
        bool playing() const { return CrabitArduboyAudio::playing(); }
    } tunes;
};

#endif // HOPPER_MYARDBUOY2_H
