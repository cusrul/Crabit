#ifndef APP_BANANONSENSE_H
#define APP_BANANONSENSE_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class BananonsenseApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 1;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = BananonsenseSandbox::State;

    const char* getAppName() override { return "Bananonsense"; }

protected:
    // The frequent in-game key is B (check the cursor), so one resolved PAT
    // tap is the primary B button and the double tap is the secondary A.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.ab.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        BananonsenseSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &BananonsenseSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.ab);
        return true;
    }

    void game_setup() override {
        BananonsenseSandbox::resetGameState();
        BananonsenseSandbox::setup();
    }
    void game_loop() override { BananonsenseSandbox::loop(); }
};

#endif
