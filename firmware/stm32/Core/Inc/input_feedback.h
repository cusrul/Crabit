#pragma once

#include "hw_ws2812.h"
#include "../../../shared/product_preferences.h"
#include "main.h"

// UI-task owner only. No new RTOS task, timer ISR or radio dependency.
class InputFeedback {
    static inline bool manual = false;
    static inline bool lit = false;
    static inline uint32_t began = 0;

   public:
    static bool manualActive() { return manual; }
    static void setManual(bool value) {
        manual = value;
        lit = false;
    }
    static void press() {
        if (manual || !CrabitPreferences::enabled(CrabitPreferences::Led) ||
            CrabitPreferences::current.led_level == 0)
            return;
        began = HAL_GetTick();
        if (!lit) {
            led_drv.setBrightness(CrabitPreferences::current.led_level);
            for (int i = 0; i < LED_COUNT; ++i)
                led_drv.setLED(i, 255, 255, 255);
            led_drv.show();
            lit = true;
        }
    }
    static void tick() {
        if (!manual && lit && uint32_t(HAL_GetTick() - began) >= 40) {
            led_drv.clear();
            led_drv.show();
            lit = false;
        }
    }
};
