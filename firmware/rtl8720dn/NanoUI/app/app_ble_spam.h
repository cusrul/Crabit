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
        enum class Mode : uint8_t { All, Android, IosDevice, IosAction, Count };
        Mode mode = Mode::All;
        const auto startMode = [&spammer](Mode selected) {
            switch (selected) {
                case Mode::Android: spammer.start(BW16BLESpammer::ANDROID_BURST); break;
                case Mode::IosDevice: spammer.start(BW16BLESpammer::IOS_NEW_DEVICE); break;
                case Mode::IosAction: spammer.start(BW16BLESpammer::IOS_ACTION_MODAL); break;
                default: spammer.start(BW16BLESpammer::ALL_BURST); break;
            }
        };
        const auto modeLabel = [](Mode selected) -> const char* {
            switch (selected) {
                case Mode::Android: return "Android";
                case Mode::IosDevice: return "iOS Device";
                case Mode::IosAction: return "iOS Action";
                default: return "All";
            }
        };

        if (!busy) {
            canvas->clear();
            canvas->drawText(2, 24, "正在启动…", 8);
            canvas->display();
            // Follow BasicSpammer.ino setup order, omitting its Serial setup
            // because this UART is the STM32 link and logging is optional.
            spammer.begin();
            spammer.setDebugLogging(false);
            spammer.setInterval(1000);
            startMode(mode);
        }

        uint32_t lastUiDraw = 0;
        bool firstUiDraw = true;
        while (is_display_active) {
            const UIEvent event = input->poll();
            if (event.type == EVT_BACK) break;

            if (!busy && (event.type == EVT_UP || event.type == EVT_DOWN)) {
                const int count = int(Mode::Count);
                const int delta = event.type == EVT_UP ? -1 : 1;
                mode = Mode((int(mode) + delta + count) % count);
                startMode(mode);
                firstUiDraw = true;
            }

            if (!busy) spammer.tick();

            const uint32_t now = millis();
            if (firstUiDraw || uint32_t(now - lastUiDraw) >= 200) {
                firstUiDraw = false;
                lastUiDraw = now;
                canvas->clear();
                canvas->drawText(2, 2, "蓝牙广播", 8);
                canvas->drawText(2, 18, modeLabel(mode), 8);
                canvas->drawText(2, 34, busy ? "无线被占用" : "运行中", 8);
                canvas->drawText(2, 50, busy ? "先关闭后台蓝牙" : "UP/DN MODE  BACK", 8);
                canvas->display();
            }
            safe_delay(10);
        }

        if (!busy) spammer.stop();
    }
};

#endif
