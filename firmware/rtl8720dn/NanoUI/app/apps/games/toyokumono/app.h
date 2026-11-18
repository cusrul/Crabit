#ifndef APP_TOYOKUMONO_H
#define APP_TOYOKUMONO_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class ToyokumonoApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 12;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = ToyokumonoSandbox::State;

    const char* getAppName() override { return "Toyokumono"; }

    void onUnmount() override {
        ToyokumonoSandbox::resetGameState();
        ArduboyCompatApp::onUnmount();
    }

protected:
    // B summons a cloud (the frequent in-game key); single tap = B, double = A.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.ab.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        ToyokumonoSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &ToyokumonoSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.ab);
        return true;
    }

    void game_setup() override {
        ToyokumonoSandbox::resetGameState();
        ToyokumonoSandbox::setup();
    }
    void game_loop() override { ToyokumonoSandbox::loop(); }
};

#endif
