#ifndef APP_ARDUGO_H
#define APP_ARDUGO_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class ArduGoApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 6;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = ArduGoSandbox::State;

    const char* getAppName() override { return "ArduGo"; }
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
        ArduGoSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &ArduGoSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        ArduGoSandbox::resetGameState();
        ArduGoSandbox::setup();
    }
    void game_loop() override { ArduGoSandbox::loop(); }
};

#endif
