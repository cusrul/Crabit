#ifndef APP_PONG_H
#define APP_PONG_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

class PongApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 18;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = PongSandbox::State;

    const char* getAppName() override { return "Pong"; }

protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        CrabitArduboy::guardZeroSave(state->arduboy.eeprom, &state->unexpectedEepromAccess);
        PongSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &PongSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->arduboy);
        return true;
    }

    void game_setup() override {
        PongSandbox::resetGameState();
        PongSandbox::setup();
    }
    void game_loop() override { PongSandbox::loop(); }
};

#endif
