#ifndef APP_SPACETANKS_H
#define APP_SPACETANKS_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class SpaceTanksApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 9;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = SpaceTanksSandbox::State;

    const char* getAppName() override { return "Space Tanks"; }

    void onUnmount() override {
        SpaceTanksSandbox::resetGameState();
        ArduboyCompatApp::onUnmount();
    }

protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->engine.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        CrabitArduboy::guardZeroSave(state->engine.arduboy.eeprom,
                                     &state->unexpectedEepromAccess);
        SpaceTanksSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &SpaceTanksSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->engine.arduboy);
        return true;
    }

    void game_setup() override {
        SpaceTanksSandbox::resetGameState();
        SpaceTanksSandbox::setup();
    }
    void game_loop() override { SpaceTanksSandbox::loop(); }
};

#endif
