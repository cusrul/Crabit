#ifndef APP_ARDUBULLETS_H
#define APP_ARDUBULLETS_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include "port.h"

class ArdubulletsApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 0;
    // Signature (4) + 10-byte record + checksum (2) at 1,008..1,023.
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = ArdubulletsSandbox::State;

    const char* getAppName() override { return "ARDUBULLET"; }

protected:
    // B is the upstream fire/menu-confirm key and stays on the single tap;
    // A (pause menu, silent sound toggle) moves to the double tap.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->engine.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        ArdubulletsSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &ArdubulletsSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->engine.arduboy);
        return true;
    }

    void game_setup() override {
        ArdubulletsSandbox::resetPortState();
        ArdubulletsSandbox::setup();
    }
    void game_loop() override { ArdubulletsSandbox::loop(); }
};

#endif
