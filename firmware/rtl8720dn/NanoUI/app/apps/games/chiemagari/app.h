#ifndef APP_CHIEMAGARI_H
#define APP_CHIEMAGARI_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#pragma push_macro("STATE_TITLE")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef FPS
#include "port.h"
#pragma pop_macro("FPS")
#pragma pop_macro("STATE_TITLE")

class ChiemagariApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 2;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = ChiemagariSandbox::State;

    const char* getAppName() override { return "Chie Magari Ita"; }
protected:
    // B is the frequent in-game key (pick/release) and confirms menus. A is
    // the rotate/cancel/menu key. Single PAT tap = B, double tap = A.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        ChiemagariSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &ChiemagariSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        ChiemagariSandbox::resetGameState();
        ChiemagariSandbox::setup();
    }
    void game_loop() override { ChiemagariSandbox::loop(); }
};

#endif
