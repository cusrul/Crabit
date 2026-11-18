#ifndef APP_MEMORYRHYTHM_H
#define APP_MEMORYRHYTHM_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

class MemoryRhythmApp : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 17;
    static constexpr uint16_t kSaveBytes = 0; // audited: no EEPROM access
    using PortState = MemoryRhythmSandbox::State;

    const char* getAppName() override { return "Memory Rhythm"; }
protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->game.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        CrabitArduboy::guardZeroSave(state->game.arduboy.eeprom,
                                     &state->unexpectedEepromAccess);
        MemoryRhythmSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &MemoryRhythmSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->game.arduboy);
        return true;
    }

    void game_setup() override {
        MemoryRhythmSandbox::resetGameState();
        MemoryRhythmSandbox::setup();
    }
    void game_loop() override { MemoryRhythmSandbox::loop(); }
};

#endif
