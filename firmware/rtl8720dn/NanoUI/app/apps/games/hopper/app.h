#ifndef APP_HOPPER_H
#define APP_HOPPER_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

// Full upstream OBONO Hopper (logo, title/menu, 50-floor levels, five platform
// types, clear/next-level, pause, record menu) running on the shared
// ArduboyCompatApp adapter. The typed State is created on demand; a second
// launch of the same port id is refused before AppManager::launch.
//
// Catalogue ports occupy ids 0..20; Hopper owns slot 21 in the shared runtime.
class AppHopper : public ArduboyCompatApp {
public:
    static constexpr uint8_t kPortId = 21;
    static_assert(kPortId < CrabitArduboy::kPortIdCount,
                  "Hopper requires its own shared runtime slot");
    // Hopper's record occupies 800..831, so the full 1 KiB logical window is
    // reserved by the existing save-slot API.
    static constexpr uint16_t kSaveBytes = 1024;
    using PortState = HopperSandbox::State;

    const char* getAppName() override { return "Hopper"; }

protected:
    bool prepareAdmission() override {
        PortState *state = CrabitArduboy::createPortState<PortState>(kPortId);
        if (!state) return false;
        if (!CrabitArduboy::attachSaveSlot(kPortId, kSaveBytes, state->engine.arduboy.eeprom)) {
            CrabitArduboy::destroyPortState(kPortId, state);
            return false;
        }
        HopperSandbox::installPortState(state);
        adoptPortState(state, &CrabitArduboy::detail::destroyTypedPortState<PortState>,
                       &HopperSandbox::clearPortState, kPortId, kSaveBytes,
                       &state->engine.arduboy);
        return true;
    }

    void game_setup() override {
        HopperSandbox::resetPortState();
        HopperSandbox::setup();
    }
    void game_loop() override { HopperSandbox::loop(); }
};

#endif // APP_HOPPER_H
