#ifndef APP_MINESWEEPER_H
#define APP_MINESWEEPER_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <string.h>
#include "port.h"

class MinesweeperApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 7;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = MinesweeperSandbox::State;

    const char* getAppName() override { return "Minesweeper"; }
protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->common.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        CrabitArduboy::guardZeroSave(state->common.arduboy.eeprom,
                                     &state->unexpectedEepromAccess);
        MinesweeperSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &MinesweeperSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->common.arduboy);
        return true;
    }

    void game_setup() override {
        MinesweeperSandbox::resetGameState();
        MinesweeperSandbox::setup();
    }
    void game_loop() override { MinesweeperSandbox::loop(); }
};

#endif
