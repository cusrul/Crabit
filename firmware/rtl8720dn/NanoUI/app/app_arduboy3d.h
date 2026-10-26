#ifndef APP_ARDUBOY3D_H
#define APP_ARDUBOY3D_H

#include "ArduboyApp.h"

#ifndef _WIN32
#define _WIN32 0
#endif

namespace Arduboy3DSandbox {
    
    #include "arduboy3d/FixedMath.cpp"
    #include "arduboy3d/Font.cpp"
    #include "arduboy3d/Draw.cpp"       
    #include "arduboy3d/Map.cpp"
    #include "arduboy3d/MapGenerator.cpp"
    #include "arduboy3d/Entity.cpp"
    #include "arduboy3d/Particle.cpp"
    #include "arduboy3d/Projectile.cpp"
    #include "arduboy3d/Enemy.cpp"
    #include "arduboy3d/Player.cpp"
    #include "arduboy3d/Sounds.cpp"
    #include "arduboy3d/Menu.cpp"
    #include "arduboy3d/Game.cpp"
    
    // 这个就是你刚才发我的 main.cpp
    #include "arduboy3d/main.cpp" 
}

class Arduboy3DApp : public ArduboyApp {
public:
    Arduboy3DApp() {
        // 直接抓取游戏自己在 main.cpp 里实例化的引擎！
        this->ab_ptr = &Arduboy3DSandbox::arduboy; 
    }
    
    const char* getAppName() override { return "ARDUBOY 3D"; }
    
    void game_setup() override {
        Arduboy3DSandbox::setup(); 
    }
    
    void game_loop() override {
        Arduboy3DSandbox::loop();  
    }
};

#endif