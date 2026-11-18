#ifndef APP_WATERNET_H
#define APP_WATERNET_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include "port.h"

class WaternetApp : public ArduboyCompatApp {
public:
    const char* getAppName() override { return "Waternet"; }
    WaternetApp() {
        port_ptr = &WaternetSandbox::arduboy;
    }
protected:
    void game_setup() override {
        WaternetSandbox::resetGameState();
        WaternetSandbox::setup();
    }
    void game_loop() override { WaternetSandbox::loop(); }
};

#endif
