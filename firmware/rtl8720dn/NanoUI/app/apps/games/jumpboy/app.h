#ifndef APP_JUMPBOY_H
#define APP_JUMPBOY_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

class JumpBoyApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 19;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = JumpBoySandbox::State;

    const char* getAppName() override { return "JumpBoy"; }

    void onUnmount() override {
        JumpBoySandbox::resetGameState();
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
        JumpBoySandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &JumpBoySandbox::clearPortState, kPortId, kSaveBytes,
                       &state->engine.arduboy);
        return true;
    }

    void game_setup() override {
        JumpBoySandbox::resetGameState();
        JumpBoySandbox::setup();
    }
    void game_loop() override { JumpBoySandbox::loop(); }
};

#endif
