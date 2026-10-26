#ifndef UART_INPUT_H
#define UART_INPUT_H

#include "../NanoUI.h"
#include "../../AmebaStore.h" // 提供输入事件队列
#include "../../AmebaServices.h"

class UartInputDriver : public InputDriver {
public:
    void init() override {
        // 虚拟驱动，无需初始化硬件
    }
    
    UIEvent poll() override {
        AmebaServices::serviceInput();
        // 直接从 AmebaStore 中读取由 UART 解析出的事件
        const int queued_event = AmebaStore::popUiEvent();
        if (queued_event != 0) {
            UIEvent e = {(EventType)queued_event, 0};
            return e;
        }
        return {EVT_NONE, 0};
    }
};

#endif
