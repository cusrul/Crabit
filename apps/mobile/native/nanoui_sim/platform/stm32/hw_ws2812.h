#ifndef CRABIT_NANOUI_SIM_HW_WS2812_H
#define CRABIT_NANOUI_SIM_HW_WS2812_H
#include <stdint.h>
constexpr int LED_COUNT = 2;
class WS2812_Driver {
public:
    struct Diagnostics {
        uint32_t supported = 0, start = 0, stop = 0, brightness = 0, grb = 0;
        uint32_t cr1 = 0, ccer = 0, ccmr1 = 0, psc = 0, arr = 0;
        uint32_t cnt_a = 0, cnt_b = 0, clock_enabled = 0;
        uint32_t pin_mode = 0, pin_af = 0, pin_type = 0;
    };
    void clear() {}
    void setBrightness(uint8_t) {}
    void setLED(int, uint8_t, uint8_t, uint8_t) {}
    void show(Diagnostics* = nullptr) {}
};
inline WS2812_Driver led_drv{};
#endif
