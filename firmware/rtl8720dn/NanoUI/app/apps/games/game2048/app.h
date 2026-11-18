#ifndef APP_GAME2048_H
#define APP_GAME2048_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <string.h>
#include "port.h"

class Game2048App : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 8;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = Game2048Sandbox::State;

    const char* getAppName() override { return "2048"; }
protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->engine.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        Game2048Sandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &Game2048Sandbox::clearPortState, kPortId, kSaveBytes,
                       &state->engine.arduboy);
        return true;
    }

    void game_setup() override {
        Game2048Sandbox::resetGameState();
        Game2048Sandbox::setup();
    }
    void game_loop() override { Game2048Sandbox::loop(); }
};

#endif
