#ifndef APP_LONGCAT_H
#define APP_LONGCAT_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

class LongCatApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 20;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = LongCatSandbox::State;

    const char* getAppName() override { return "LongCat"; }

    void onUnmount() override {
        LongCatSandbox::resetGameState();
        ArduboyCompatApp::onUnmount();
    }

protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        Arduboy2Compat &engine = state->game.getArduboy();
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, engine.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        LongCatSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &LongCatSandbox::clearPortState, kPortId, kSaveBytes, &engine);
        return true;
    }

    void game_setup() override {
        LongCatSandbox::resetGameState();
        LongCatSandbox::portState().game.setup();
    }
    void game_loop() override { LongCatSandbox::portState().game.loop(); }
};

#endif
