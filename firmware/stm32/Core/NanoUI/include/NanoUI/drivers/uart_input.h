#ifndef UART_INPUT_H
#define UART_INPUT_H

#include "../src/NanoUI.h"

// 引入全局虚拟按键状态
extern volatile UIEventType g_virtual_btn;

class UartInputDriver : public InputDriver {
public:
    void init() override {
        // 不需要初始化引脚，因为是虚拟的！
    }
    
    UIEvent poll() override {
        // 如果串口解析出了按键，把它装包发给 NanoUI
        if (g_virtual_btn != EVT_NONE) {
            UIEvent e = {g_virtual_btn, 0};
            g_virtual_btn = EVT_NONE; // 拿走后就消费掉(清空)
            return e;
        }
        return {EVT_NONE, 0};
    }
};

#endif