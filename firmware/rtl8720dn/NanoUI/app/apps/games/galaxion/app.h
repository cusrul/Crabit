#ifndef APP_GALAXION_H
#define APP_GALAXION_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class GalaxionApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 10;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = GalaxionSandbox::State;

    const char* getAppName() override { return "Galaxion"; }

    void onUnmount() override {
        GalaxionSandbox::resetGameState();
        ArduboyCompatApp::onUnmount();
    }

protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->mArduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        CrabitArduboy::guardZeroSave(state->mArduboy.eeprom, &state->unexpectedEepromAccess);
        GalaxionSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &GalaxionSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->mArduboy);
        return true;
    }

    void game_setup() override {
        GalaxionSandbox::resetGameState();
        GalaxionSandbox::setup();
    }
    void game_loop() override { GalaxionSandbox::loop(); }
};

#endif
