#ifndef APP_AUDIO_SETTINGS_H
#define APP_AUDIO_SETTINGS_H

#include "NanoUI/NanoUI.h"
#include "global_store.h"

class AppAudioSettings : public Application {
   public:
    const char* getAppName() override { return "AUDIO_SET"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;

        while (running) {
            UIEvent e = input->poll();

            if (e.type != EVT_NONE) {
                // 璋冭妭闊抽噺 (姝ヨ繘 10)
                if (e.type == EVT_RIGHT || e.type == EVT_UP) {
                    if (GlobalStore.sys_volume <= 90)
                        GlobalStore.sys_volume += 10;
                    else
                        GlobalStore.sys_volume = 100;
                    // 璇曢煶
                    SysAudio::play(3000, 30);
                } else if (e.type == EVT_LEFT || e.type == EVT_DOWN) {
                    if (GlobalStore.sys_volume >= 10)
                        GlobalStore.sys_volume -= 10;
                    else
                        GlobalStore.sys_volume = 0;
                    // 璇曢煶
                    if (GlobalStore.sys_volume > 0)
                        SysAudio::play(3000, 30);
                } else if (e.type == EVT_BACK || e.type == EVT_OK) {
                    running = false;
                    break;
                }
            }

            // --- 鏋佽嚧 UI 娓叉煋 ---
            canvas->clear();
            canvas->fillRect(0, 0, 128, 14, C_WHITE);
            canvas->drawText(5, 3, "SYSTEM AUDIO", 1, true);

            canvas->drawText(10, 25, "Volume:", 1);

            // 鐢婚煶閲忚繘搴︽潯澶栨
            canvas->drawRect(10, 40, 100, 10, C_WHITE);
            // 濉厖褰撳墠闊抽噺
            if (GlobalStore.sys_volume > 0) {
                canvas->fillRect(10, 40, GlobalStore.sys_volume, 10, C_WHITE);
            }

            // 鐧惧垎姣旀枃瀛?
            char buf[8];
            if (GlobalStore.sys_volume == 0) {
                canvas->drawText(60, 25, "MUTE", 1);
            } else {
                snprintf(buf, sizeof(buf), "%d%%", GlobalStore.sys_volume);
                canvas->drawText(60, 25, buf, 1);
            }

            canvas->display();
            vTaskDelay(pdMS_TO_TICKS(16));
        }

        AppManager::exitCurrentApp();
    }
};

#endif
