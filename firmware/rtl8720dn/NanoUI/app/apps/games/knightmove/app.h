#ifndef APP_KNIGHTMOVE_H
#define APP_KNIGHTMOVE_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include "port.h"

class KnightMoveApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 5;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = KnightMoveSandbox::State;

    const char* getAppName() override { return "Knight Move"; }
protected:
    // B rolls the dice (the frequent in-game key); single tap = B, double = A.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        KnightMoveSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &KnightMoveSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        KnightMoveSandbox::resetPortState();
        KnightMoveSandbox::setup();
    }
    void game_loop() override { KnightMoveSandbox::loop(); }
};

#endif
