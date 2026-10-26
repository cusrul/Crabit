#ifndef APP_BUZZER_TEST_H
#define APP_BUZZER_TEST_H

#include "NanoUI/NanoUI.h"
#include "tim.h" // 拿到 htim15

class AppBuzzerTest : public Application {
private:
    int freq = 2700;       // 鍒濆鐩爣棰戠巼 2700Hz
    int duty_percent = 50; // 鍒濆鍗犵┖姣?50%
    bool is_playing = false;

    // 馃専 鏍稿績锛氱洿鎺ユ搷鎺у簳灞傚瘎瀛樺櫒骞跺己鍒跺埛鏂扮殑璋冮煶寮曟搸
    void applySettings() {
        if (freq < 100) freq = 100;
        if (freq > 10000) freq = 10000;
        if (duty_percent < 0) duty_percent = 0;
        if (duty_percent > 100) duty_percent = 100;

        // 1. 璁剧疆棰勫垎棰戝櫒涓?79锛屽皢 80MHz 绯荤粺鏃堕挓闄嶅埌 1MHz
        __HAL_TIM_SET_PRESCALER(&htim15, 79);

        // 2. 璁＄畻鍛ㄦ湡鍊?ARR (1,000,000 / 棰戠巼)
        uint32_t arr_val = 1000000 / freq;
        __HAL_TIM_SET_AUTORELOAD(&htim15, arr_val);

        // 3. 璁＄畻鍗犵┖姣?Compare (ARR * 鍗犵┖姣旂櫨鍒嗘瘮)
        uint32_t cmp_val = 0;
        if (is_playing) {
            cmp_val = (arr_val * duty_percent) / 100;
        }
        __HAL_TIM_SET_COMPARE(&htim15, TIM_CHANNEL_2, cmp_val);

        // 馃挜 鐮村眬鏍稿績锛氫骇鐢熸洿鏂颁簨浠讹紝寮哄埗鍒锋柊褰卞瓙瀵勫瓨鍣紝绔嬪嵆鐢熸晥锛?
        htim15.Instance->EGR = TIM_EGR_UG;
    }

public:
    const char* getAppName() override { return "BUZZER_TEST"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        
        // 鍚姩 PWM 杈撳嚭閫氶亾
        HAL_TIM_PWM_Start(&htim15, TIM_CHANNEL_2);
        applySettings();

        while (running) {
            UIEvent e = input->poll();
            
            // 鎸変笅 BACK 閫€鍑?
            if (e.type == EVT_BACK) {
                is_playing = false;
                applySettings(); // 璧颁箣鍓嶅厛闂槾
                running = false;
                break;
            }

            // 浜や簰閫昏緫
            if (e.type != EVT_NONE) {
                if (e.type == EVT_UP)        freq += 100;
                else if (e.type == EVT_DOWN) freq -= 100;
                else if (e.type == EVT_RIGHT) duty_percent += 10;
                else if (e.type == EVT_LEFT)  duty_percent -= 10;
                else if (e.type == EVT_OK)    is_playing = !is_playing;
                
                applySettings(); // 绔嬪埢搴旂敤鏂板０闊?
            }

            // UI 缁樺埗
            canvas->clear();
            canvas->fillRect(0, 0, 128, 14, C_WHITE);
            canvas->drawText(2, 3, "[ PWM AUDIO TUNER ]", 1, true);

            char buf[32];
            
            // 鏄剧ず鐘舵€?
            if (is_playing) {
                canvas->drawText(10, 20, "Status: >> PLAYING <<", 1);
            } else {
                canvas->drawText(10, 20, "Status: MUTED", 1);
            }

            // 鏄剧ず棰戠巼
            snprintf(buf, sizeof(buf), "Freq: %d Hz", freq);
            canvas->drawText(10, 35, buf, 1);

            // 鏄剧ず鍗犵┖姣?
            snprintf(buf, sizeof(buf), "Duty: %d %%", duty_percent);
            canvas->drawText(10, 50, buf, 1);

            canvas->display();
            vTaskDelay(pdMS_TO_TICKS(16));
        }

        HAL_TIM_PWM_Stop(&htim15, TIM_CHANNEL_2);
        AppManager::exitCurrentApp();
    }
};

#endif

