#ifndef APP_ARDUBOY3D_H
#define APP_ARDUBOY3D_H

#include "../../../compat/arduboy/ArduboyCompatApp.h"

#ifndef _WIN32
#define _WIN32 0
#endif

namespace Arduboy3DSandbox {

    using Arduboy2 = Arduboy2Compat;
    using Sprites = SpritesCompat;
    using ArduboyTones = ArduboyTonesCompat;

    #include "FixedMath.cpp"
    #include "Font.cpp"
    #include "Draw.cpp"
    #include "Map.cpp"
    #include "MapGenerator.cpp"
    #include "Entity.cpp"
    #include "Particle.cpp"
    #include "Projectile.cpp"
    #include "Enemy.cpp"
    #include "Player.cpp"
    #include "Sounds.cpp"
    #include "Menu.cpp"
    #include "Game.cpp"

    // 这个就是你刚才发我的 main.cpp
    #include "main.cpp"
}

class Arduboy3DApp : public ArduboyCompatApp {
public:
    Arduboy3DApp() {
        // 直接抓取游戏自己在 main.cpp 里实例化的引擎！
        this->port_ptr = &Arduboy3DSandbox::arduboy;
    }

    const char* getAppName() override { return "ARDUBOY 3D"; }

protected:
    // B fires (the frequent in-game key). A is the original held strafe
    // modifier; the PAT adapter only supplies taps, so A is the double tap.
    uint8_t primaryActionButton() const override { return B_BUTTON; }
    uint8_t secondaryActionButton() const override { return A_BUTTON; }

public:
    void game_setup() override {
        Arduboy3DSandbox::setup();
    }

    void game_loop() override {
        Arduboy3DSandbox::loop();
    }
};

#endif
