#ifndef DRIVER_BUTTON_H
#define DRIVER_BUTTON_H

#include "../hal/input.h"
#include <Arduino.h>
#include "../hal/hal_time.h"
class ButtonDriver : public InputDriver {
private:
    struct BtnState {
        int pin;
        EventType evt;
        int state;
        int lastReading;
        unsigned long lastDebounceTime;
        
        uint32_t pressStartTime;
        bool longPressHandled;
        uint32_t lastRepeatTime;
    };
    
    BtnState buttons[4]; 
    
    const uint32_t DEBOUNCE_DELAY = 50;
    const uint32_t LONG_PRESS_MS = 800;
    const uint32_t REPEAT_DELAY_MS = 500;
    const uint32_t REPEAT_INTERVAL = 150;

public:
    // 【构造函数 1】全功能 4 按键
    ButtonDriver(int pinUp, int pinDown, int pinOk, int pinBack) {
        initButtons(pinUp, pinDown, pinOk, pinBack);
    }

    // 【构造函数 2】兼容 3 按键 (自动禁用 Back 键)
    ButtonDriver(int pinUp, int pinDown, int pinOk) {
        initButtons(pinUp, pinDown, pinOk, -1); // -1 代表不存在
    }

    void initButtons(int pinUp, int pinDown, int pinOk, int pinBack) {
        buttons[0] = {pinUp,   EVT_UP,   HIGH, HIGH, 0, 0, false, 0};
        buttons[1] = {pinDown, EVT_DOWN, HIGH, HIGH, 0, 0, false, 0};
        buttons[2] = {pinOk,   EVT_OK,   HIGH, HIGH, 0, 0, false, 0};
        buttons[3] = {pinBack, EVT_BACK, HIGH, HIGH, 0, 0, false, 0};
    }

    void init() override {
        for(int i=0; i<4; i++) {
            // 【安全检查】只有引脚号有效才初始化
            if (buttons[i].pin >= 0) {
                pinMode(buttons[i].pin, INPUT_PULLUP);
            }
        }
    }

    UIEvent poll() override {
        uint32_t now = hal_get_ticks();
        UIEvent detected = {EVT_NONE, 0};

        for(int i=0; i<4; i++) {
            BtnState &btn = buttons[i];

            // 【安全检查】如果引脚是 -1，直接跳过，当做没发生
            if (btn.pin < 0) continue;

            int reading = digitalRead(btn.pin);

            if (reading != btn.lastReading) btn.lastDebounceTime = now;
            btn.lastReading = reading;

            if ((now - btn.lastDebounceTime) > DEBOUNCE_DELAY) {
                if (reading != btn.state) {
                    btn.state = reading;

                    if (btn.state == LOW) { // 按下
                        btn.pressStartTime = now;
                        btn.longPressHandled = false;
                        btn.lastRepeatTime = now + REPEAT_DELAY_MS;

                        if (btn.evt == EVT_UP || btn.evt == EVT_DOWN || btn.evt == EVT_BACK) {
                            detected = {btn.evt, 0};
                        }
                    }
                    else { // 松开
                        if (btn.evt == EVT_OK && !btn.longPressHandled) {
                            detected = {EVT_OK, 0};
                        }
                    }
                }
                
                // 连发与长按处理
                if (btn.state == LOW) {
                    if (btn.evt == EVT_UP || btn.evt == EVT_DOWN) {
                        if (now > btn.lastRepeatTime) {
                            btn.lastRepeatTime = now + REPEAT_INTERVAL;
                            return {btn.evt, 0};
                        }
                    }
                    if (btn.evt == EVT_OK && !btn.longPressHandled && (now - btn.pressStartTime > LONG_PRESS_MS)) {
                        btn.longPressHandled = true;
                        return {EVT_OK_LONG, 0};
                    }
                }
            }
            
            if (detected.type != EVT_NONE) return detected;
        }
        
        return {EVT_NONE, 0};
    }
};
#endif