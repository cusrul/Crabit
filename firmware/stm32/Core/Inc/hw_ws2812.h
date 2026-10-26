#ifndef HW_WS2812_H
#define HW_WS2812_H

#include <stdint.h>
#include <string.h>

#if defined(USE_HAL_DRIVER) && defined(STM32L432xx)
#include "tim.h"
#include "led_rail_stop2.h"
#endif

static constexpr int LED_COUNT = 2;

class WS2812_Driver {
private:
    struct RGB {
        uint8_t r;
        uint8_t g;
        uint8_t b;
    };

    RGB leds[LED_COUNT];
    uint8_t brightness = 0;

    static uint8_t scaleChannel(uint8_t value, uint8_t bright) {
        return (uint8_t)((value * bright + 50) / 100);
    }

public:
    struct Diagnostics {
        uint32_t supported = 0;
        uint32_t start = 0xFFFFFFFFu, stop = 0xFFFFFFFFu;
        uint32_t cr1 = 0, ccer = 0, ccmr1 = 0, psc = 0, arr = 0;
        uint32_t cnt_a = 0, cnt_b = 0, clock_enabled = 0;
        uint32_t pin_mode = 0, pin_af = 0, pin_type = 0;
        uint32_t brightness = 0, grb = 0;
    };

    WS2812_Driver() { clear(); }

    void clear() {
        memset(leds, 0, sizeof(leds));
        brightness = 0;
    }

    void setBrightness(uint8_t value) {
        brightness = value;
    }

    void setLED(int index, uint8_t r, uint8_t g, uint8_t b) {
        if (index < 0 || index >= LED_COUNT) return;
        leds[index] = {r, g, b};
    }

    void show(Diagnostics* diagnostics = nullptr) {
        if (diagnostics) {
            *diagnostics = Diagnostics{};
            diagnostics->brightness = brightness;
            diagnostics->grb = (uint32_t(scaleChannel(leds[0].g, brightness)) << 16) |
                (uint32_t(scaleChannel(leds[0].r, brightness)) << 8) |
                scaleChannel(leds[0].b, brightness);
        }
    #if defined(USE_HAL_DRIVER) && defined(STM32L432xx)
        // A failed supply restore must not drive an unpowered WS2812 DIN.
        if (LedRailStop2::restoreFault.load(std::memory_order_relaxed)) return;
        __HAL_TIM_SET_AUTORELOAD(&htim2, 99);
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0);
        const HAL_StatusTypeDef start_result = HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
        if (diagnostics) {
            // Observe while enabled, before the existing counter reset. No
            // logging, delays, retries, GPIO writes or changes inside bit output.
            diagnostics->supported = 1;
            diagnostics->start = start_result;
            diagnostics->cnt_a = TIM2->CNT;
            diagnostics->cr1 = TIM2->CR1;
            diagnostics->ccer = TIM2->CCER;
            diagnostics->ccmr1 = TIM2->CCMR1;
            diagnostics->psc = TIM2->PSC;
            diagnostics->arr = TIM2->ARR;
            diagnostics->clock_enabled = (RCC->APB1ENR1 & RCC_APB1ENR1_TIM2EN) ? 1u : 0u;
            diagnostics->pin_mode = (GPIOA->MODER >> 10) & 3u;
            diagnostics->pin_af = (GPIOA->AFR[0] >> 20) & 15u;
            diagnostics->pin_type = (GPIOA->OTYPER >> 5) & 1u;
            diagnostics->cnt_b = TIM2->CNT;
        }

        constexpr uint32_t kT0H = 28;
        constexpr uint32_t kT1H = 56;

        __disable_irq();
        TIM2->CNT = 0;
        TIM2->SR &= ~TIM_SR_UIF;

        auto waitOneBit = []() {
            while ((TIM2->SR & TIM_SR_UIF) == 0) {}
            TIM2->SR &= ~TIM_SR_UIF;
        };

        auto sendBit = [&](bool one) {
            TIM2->CCR1 = one ? kT1H : kT0H;
            waitOneBit();
        };

        auto sendByte = [&](uint8_t data) {
            for (int bit = 7; bit >= 0; --bit) {
                sendBit((data >> bit) & 0x01);
            }
        };

        for (int i = 0; i < LED_COUNT; ++i) {
            const uint8_t r = scaleChannel(leds[i].r, brightness);
            const uint8_t g = scaleChannel(leds[i].g, brightness);
            const uint8_t b = scaleChannel(leds[i].b, brightness);
            sendByte(g);
            sendByte(r);
            sendByte(b);
        }

        TIM2->CCR1 = 0;
        while ((TIM2->SR & TIM_SR_UIF) == 0) {}
        TIM2->SR &= ~TIM_SR_UIF;
        const HAL_StatusTypeDef stop_result = HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
        __enable_irq();
        if (diagnostics) diagnostics->stop = stop_result;
    #else
        // Desktop/sim build keeps the software state only.
        (void)brightness;
    #endif
    }
};

extern WS2812_Driver led_drv;

#endif
