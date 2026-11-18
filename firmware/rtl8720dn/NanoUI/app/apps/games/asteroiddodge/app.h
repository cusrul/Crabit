#ifndef APP_ASTEROIDDODGE_H
#define APP_ASTEROIDDODGE_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

class AsteroidDodgeApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 16;
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = AsteroidDodgeSandbox::State;

    const char* getAppName() override { return "Asteroid Dodge"; }
protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->game.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        AsteroidDodgeSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &AsteroidDodgeSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->game.arduboy);
        return true;
    }

    void game_setup() override {
        AsteroidDodgeSandbox::resetGameState();
        AsteroidDodgeSandbox::setup();
    }
    void game_loop() override { AsteroidDodgeSandbox::loop(); }
};

#endif
