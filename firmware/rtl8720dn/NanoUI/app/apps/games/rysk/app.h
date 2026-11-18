#ifndef APP_RYSK_H
#define APP_RYSK_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class RyskApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 13;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = RyskSandbox::State;

    const char* getAppName() override { return "RYSK"; }
protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        CrabitArduboy::guardZeroSave(state->common.arduboy.eeprom,
                                     &state->unexpectedEepromAccess);
        RyskSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &RyskSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        RyskSandbox::resetGameState();
        RyskSandbox::setup();
    }
    void game_loop() override { RyskSandbox::loop(); }
};

#endif
