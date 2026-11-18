#ifndef APP_EVASION_H
#define APP_EVASION_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include "port.h"

class EvasionApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 4;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = EvasionSandbox::State;

    const char* getAppName() override { return "Evasion"; }
protected:
    // B is the frequent confirm/restart key; single tap = B, double tap = A.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        EvasionSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &EvasionSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        EvasionSandbox::resetPortState();
        EvasionSandbox::setup();
    }
    void game_loop() override { EvasionSandbox::loop(); }
};

#endif
