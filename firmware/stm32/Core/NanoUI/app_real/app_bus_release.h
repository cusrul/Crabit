#ifndef APP_BUS_RELEASE_H
#define APP_BUS_RELEASE_H

#include "NanoUI/NanoUI.h"

// 寮曠敤搴曞眰鐨?C 鍑芥暟
extern "C" void BW16_Release_Bus();
extern "C" void BW16_Restore_Bus();

class BusReleaseApp : public Application {
public:
    const char* getAppName() override { return "UART_BYPASS"; }

    // 鏋勯€犲嚱鏁帮細杩涘叆椤甸潰锛孲TM32 绔嬪嵆璁╀綅
    BusReleaseApp() {
        BW16_Release_Bus();
    }

    // 鏋愭瀯鍑芥暟锛氱寮€椤甸潰锛堟寜 BACK锛夛紝STM32 閲嶆柊鎷垮洖鎺у埗鏉?
    ~BusReleaseApp() {
        BW16_Restore_Bus();
    }

    Widget* buildUI() override {
        return new Container(
            new Column(NanoUI::EmbeddedVector<Widget*>{
                new Spacer(),
                new Text("[ BYPASS MODE ]", 1, true),
                new SizedBox(0, 6),
                new Text("STM32 is Invisible", 1),
                new Text("UART Pins Released", 1),
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

