#ifndef APP_REVERSI_H
#define APP_REVERSI_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma push_macro("STATE_TITLE")
#undef STATE_TITLE
#include "port.h"
#pragma pop_macro("STATE_TITLE")

class ReversiApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 14;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = ReversiSandbox::State;

    const char* getAppName() override { return "Reversi"; }
protected:
    // B places a stone (the frequent in-game key); single tap = B, double = A.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        ReversiSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &ReversiSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        ReversiSandbox::resetGameState();
        ReversiSandbox::setup();
    }
    void game_loop() override { ReversiSandbox::loop(); }
};

#endif
