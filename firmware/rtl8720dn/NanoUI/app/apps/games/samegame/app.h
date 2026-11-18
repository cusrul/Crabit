#ifndef APP_SAMEGAME_H
#define APP_SAMEGAME_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#pragma push_macro("STATE_TITLE")
#pragma push_macro("FPS")
#undef STATE_TITLE
#undef FPS
#include "port.h"
#pragma pop_macro("FPS")
#pragma pop_macro("STATE_TITLE")

class SameGameApp : public ArduboyCompatApp {
public:
    const char* getAppName() override { return "SameGame"; }
    SameGameApp() {
        port_ptr = &SameGameSandbox::arduboy;
    }
protected:
    // B removes the selected group (the frequent in-game key); single = B.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

    void game_setup() override {
        SameGameSandbox::resetGameState();
        SameGameSandbox::setup();
    }
    void game_loop() override { SameGameSandbox::loop(); }
};

#endif
