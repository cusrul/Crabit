#ifndef APP_BUS_RELEASE_H
#define APP_BUS_RELEASE_H

#include "NanoUI/NanoUI.h"

// 独占闸门：进入前先取得 USART1/CDC 所有权，退出时恢复普通 DMA RX。
extern "C" bool BW16_Enter_Bypass();
extern "C" bool BW16_Exit_Bypass();

class BusReleaseApp : public Application {
    bool entered_ = false;
    bool gate_failed_ = false;

public:
    const char* getAppName() override { return "UART_BYPASS"; }

    // 进入页面：STM32 让位前必须先过独占闸门。
    void onCreate() override {
        entered_ = BW16_Enter_Bypass();
        gate_failed_ = !entered_;
        if (gate_failed_) Popup::showToast("UART BUSY");
    }

    // 离开页面：恢复失败时保持 FAULT 并提示，不静默重开普通流量。
    void onDestroy() override {
        if (!entered_) return;
        if (!BW16_Exit_Bypass()) Popup::showToast("UART RESTORE FAIL");
        entered_ = false;
    }

    Widget* buildUI() override {
        return new Container(
            new Column(NanoUI::EmbeddedVector<Widget*>{
                new Spacer(),
                new Text("[ BYPASS MODE ]", 1, true),
                new SizedBox(0, 6),
                new Text(gate_failed_ ? "UART BUSY" : "STM32 is Invisible", 1),
                new Text(gate_failed_ ? "Gate not claimed" : "UART Pins Released", 1),
                new SizedBox(0, 6),
                new Text("Use external tool", 1),
                new Text("to flash BW16 now", 1),
                new Spacer(),
                new Text("< PRESS BACK TO EXIT", 1)
            }),
            128, 64, 0, true
        );
    }
};

#endif
