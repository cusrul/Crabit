#ifndef APP_STAIRSSWEEP_H
#define APP_STAIRSSWEEP_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class StairssweepApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 11;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = StairssweepSandbox::State;

    const char* getAppName() override { return "Stairssweep"; }

    void onUnmount() override {
        StairssweepSandbox::resetGameState();
        ArduboyCompatApp::onUnmount();
    }

protected:
    // B rotates the falling bunch (the frequent in-game key); single = B.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        StairssweepSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &StairssweepSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        StairssweepSandbox::resetGameState();
        StairssweepSandbox::setup();
    }
    void game_loop() override { StairssweepSandbox::loop(); }
};

#endif
