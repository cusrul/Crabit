#ifndef ARDUGO_PORT_ARDUGO_H
#define ARDUGO_PORT_ARDUGO_H

/*
 *  File: ArduGo.h
 *  Purpose: Declaration of game-wide assets.
 *  Author: Don Anderson
 *
 *  BW16 port note: the upstream file includes <Arduboy.h> and declares an
 *  Arduboy/ArduboyTunes pair. The shared Arduboy2Compat surface is included by
 *  app.h; this header only adds the two helpers the original sketch used.
 */

#include "defines-local.h"

PROGMEM const char* const version = "0.0";

typedef uint8_t bool_t;
const bool_t T = 1;
const bool_t F = 0;

// Thin compat engine: the upstream code calls start() and println(); both are
// harmless additions over Arduboy2Compat and keep the game source readable.
class ArduGoArduboy : public Arduboy2Compat {
public:
    void start() { beginNoLogo(); }
    size_t println() { return write('\n'); }
    template <typename U> size_t println(U value) {
        size_t count = print(value);
        count += write('\n');
        return count;
    }
};

// Only the original two beeps are used; they go through the shared mono
// adapter and are emitted on the App audio tick.
struct ArduGoTunes {
    void tone(uint16_t frequency, uint16_t durationMs) {
        if (CrabitArduboyAudio::enabled())
            CrabitArduboyAudio::playTone(frequency, durationMs,
                                         CrabitArduboyAudio::PriorityEffect);
    }
};

extern ArduGoArduboy arduboy;
extern ArduGoTunes tunes;

extern uint8_t previousButtonState;
extern uint8_t currentButtonState;
extern uint8_t injectButtonState;

extern uint8_t black_live, white_live;
extern uint16_t black_capt, white_capt;

void injectButton(uint8_t button);
bool_t wasJustPressed(uint8_t button);

void ardugo_setup();

void
drawBitmap(uint16_t x, uint16_t y, const uint8_t *bitmap);

#endif
