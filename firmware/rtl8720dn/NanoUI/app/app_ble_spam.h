#ifndef CRABIT_APP_BLE_SPAM_H
#define CRABIT_APP_BLE_SPAM_H

#include "../src/NanoUI.h"
#include "../RadioSvc.h"
#include <BW16BLESpam.h>

class AppBleSpam : public Application {
public:
    const char* getAppName() override { return "蓝牙广播"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        const RadioSvc::Snapshot owner = RadioSvc::snapshot();
        // initialized means RadioSvc queues/owner context were set up in
        // AmebaServices::init(); it does not mean the radio is currently owned.
        const bool busy = owner.state != RadioSvc::IDLE ||
                          RadioSvc::companionActive();
        BW16BLESpammer spammer;

        if (!busy) {
            canvas->clear();
            canvas->drawText(2, 24, "正在启动…", 8);
            canvas->display();
            // Follow BasicSpammer.ino setup order, omitting its Serial setup
            // because this UART is the STM32 link and logging is optional.
            spammer.begin();
            spammer.setDebugLogging(false);
            spammer.setInterval(1000);
            spammer.start(BW16BLESpammer::ALL_BURST);
        }

        uint32_t lastUiDraw = 0;
        bool firstUiDraw = true;
        while (is_display_active) {
            const UIEvent event = input->poll();
            if (event.type == EVT_BACK) break;

            if (!busy) spammer.tick();

            const uint32_t now = millis();
            if (firstUiDraw || uint32_t(now - lastUiDraw) >= 200) {
                firstUiDraw = false;
                lastUiDraw = now;
                canvas->clear();
                canvas->drawText(2, 2, "蓝牙广播", 8);
                canvas->drawText(2, 18, "全部类型 每秒轮换", 8);
                canvas->drawText(2, 34, busy ? "无线被占用" : "运行中", 8);
                canvas->drawText(2, 50, busy ? "先关闭后台蓝牙" : "返回键停止并退出", 8);
                canvas->display();
            }
            safe_delay(10);
        }

        if (!busy) spammer.stop();
    }
};

#endif
