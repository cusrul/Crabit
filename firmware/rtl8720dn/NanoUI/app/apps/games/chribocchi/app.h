#ifndef APP_CHRIBOCCHI_H
#define APP_CHRIBOCCHI_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include "port.h"

class ChribocchiApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 3;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = ChribocchiSandbox::State;

    const char* getAppName() override { return "Chri-Bocchi Cat"; }
protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->engine.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        ChribocchiSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &ChribocchiSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->engine.arduboy);
        return true;
    }

    void game_setup() override {
        ChribocchiSandbox::resetGameState();
        ChribocchiSandbox::setup();
    }
    void game_loop() override { ChribocchiSandbox::loop(); }
};

#endif
