#ifndef APP_MONTANA_H
#define APP_MONTANA_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "port.h"

class MontanaApp : public ArduboyCompatApp {
public:
    const char* getAppName() override { return "Montana"; }
    MontanaApp() {
        port_ptr = &MontanaSandbox::arduboy;
    }
protected:
    void game_setup() override {
        MontanaSandbox::resetGameState();
        MontanaSandbox::setup();
    }
    void game_loop() override { MontanaSandbox::loop(); }
};

#endif
