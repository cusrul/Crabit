#ifndef CASTLE_BOY_APP_H
#define CASTLE_BOY_APP_H

// 引用刚才新建的正确底座
#include "ArduboyApp.h" 

// ==========================================
// 1. 建立隔离沙盒
// ==========================================
namespace CastleBoySandbox {
    
    Arduboy2 ab;
    Sprites sprites(ab);
    ArduboyTones sound;
    uint8_t mainState = 0;
    uint8_t flashCounter = 0;

    namespace Util {
        void toggle(uint8_t & flags, uint8_t mask) { flags ^= mask; }
        bool collideRect(int16_t x1, int8_t y1, uint8_t w1, uint8_t h1, int16_t x2, int8_t y2, uint8_t w2, uint8_t h2) {
            return !(x1 >= x2 + w2 || x1 + w1 <= x2 || y1 >= y2 + h2 || y1 + h1 <= y2);
        }
        void drawNumber(int16_t x, int16_t y, uint16_t value, uint8_t align) {
            char buf[12];
            snprintf(buf, sizeof(buf), "%u", value);
            uint8_t len = strlen(buf);
            if (align == ALIGN_CENTER) x -= (len * 6) / 2;
            else if (align == ALIGN_RIGHT) x -= (len * 6);
            ab.setCursor(x, y);
            ab.print(buf);
        }
    }

    // 引入所有源码
    #include "castle_boy/map.cpp"
    #include "castle_boy/entity.cpp"
    #include "castle_boy/player.cpp"
    #include "castle_boy/game.cpp"
    #include "castle_boy/menu.cpp"

    // 包含 main.cpp
    #include "castle_boy/main.cpp" 
}

// ==========================================
// 2. 注册为 NanoUI 应用
// ==========================================
class CastleBoyApp : public ArduboyApp {
public:
    CastleBoyApp() {
        // 绑定到对应指针变量
        this->ab_ptr = &CastleBoySandbox::ab; 
    }

    const char* getAppName() override { return "CASTLE BOY"; }
    
    void game_setup() override {
        CastleBoySandbox::setup(); 
    }
    
    void game_loop() override {
        CastleBoySandbox::loop();  
    }
};

#endif