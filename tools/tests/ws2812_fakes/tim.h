#ifndef WS2812_TEST_FAKE_TIM_H
#define WS2812_TEST_FAKE_TIM_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

enum HAL_StatusTypeDef : uint32_t {
    HAL_OK = 0,
    HAL_ERROR = 1,
    HAL_BUSY = 2,
};

constexpr uint32_t TIM_CHANNEL_1 = 1u;
constexpr uint32_t TIM_CR1_CEN = 1u;
constexpr uint32_t TIM_CCER_CC1E = 1u;
constexpr uint32_t TIM_SR_UIF = 1u;
constexpr uint32_t RCC_APB1ENR1_TIM2EN = 1u << 0;

struct FakeRegister {
    uint32_t value = 0;
    uint32_t reads = 0;
    uint32_t writes = 0;

    operator uint32_t() {
        ++reads;
        return value;
    }

    FakeRegister& operator=(uint32_t next) {
        value = next;
        ++writes;
        return *this;
    }
};

struct FakeCounterRegister {
    uint32_t value = 0;
    uint32_t reads = 0;
    uint32_t writes = 0;
    uint32_t reads_before_reset = 0;
    bool reset_written = false;

    operator uint32_t() {
        ++reads;
        if (!reset_written)
            ++reads_before_reset;
        const uint32_t snapshot = value;
        ++value;
        return snapshot;
    }

    FakeCounterRegister& operator=(uint32_t next) {
        value = next;
        ++writes;
        if (next == 0)
            reset_written = true;
        return *this;
    }
};

struct FakeTimer;

struct FakeStatusRegister {
    uint32_t value = 0;
    uint32_t reads = 0;
    uint32_t writes = 0;
    FakeTimer* timer = nullptr;

    operator uint32_t();
    FakeStatusRegister& operator&=(uint32_t mask);
};

struct FakeCompareRegister {
    uint32_t value = 0;
    uint32_t reads = 0;
    uint32_t writes = 0;
    std::array<uint32_t, 64> written_values{};

    operator uint32_t() {
        ++reads;
        return value;
    }

    FakeCompareRegister& operator=(uint32_t next) {
        value = next;
        if (writes < written_values.size())
            written_values[writes] = next;
        ++writes;
        return *this;
    }
};

struct FakeTimer {
    FakeRegister CR1;
    FakeRegister CCER;
    FakeRegister CCMR1;
    FakeRegister PSC;
    FakeRegister ARR;
    FakeCounterRegister CNT;
    FakeStatusRegister SR;
    FakeCompareRegister CCR1;
};

inline FakeStatusRegister::operator uint32_t() {
    ++reads;
    if (!timer || (timer->CR1.value & TIM_CR1_CEN) == 0)
        std::abort();
    uint32_t observed = value;
    observed |= TIM_SR_UIF;
    return observed;
}

inline FakeStatusRegister& FakeStatusRegister::operator&=(uint32_t mask) {
    const uint32_t observed = static_cast<uint32_t>(*this);
    value = observed & mask;
    ++writes;
    return *this;
}

struct FakeRcc {
    FakeRegister APB1ENR1;
};

struct FakeGpio {
    FakeRegister MODER;
    FakeRegister OTYPER;
    FakeRegister AFR[2];
};

struct TIM_HandleTypeDef {
    FakeTimer* Instance = nullptr;
};

extern FakeTimer fakeTimer;
extern FakeRcc fakeRcc;
extern FakeGpio fakeGpio;
extern TIM_HandleTypeDef htim2;
extern HAL_StatusTypeDef fakeStartStatus;
extern HAL_StatusTypeDef fakeStopStatus;
extern uint32_t fakeStartCalls;
extern uint32_t fakeStopCalls;
extern uint32_t fakeStartChannel;
extern uint32_t fakeStopChannel;
extern bool fakeStartSawRunning;
extern uint32_t fakeDisableIrqCalls;
extern uint32_t fakeEnableIrqCalls;
extern bool fakeInterruptsEnabled;

#define TIM2 (&fakeTimer)
#define RCC (&fakeRcc)
#define GPIOA (&fakeGpio)
#define __HAL_TIM_SET_AUTORELOAD(handle, value) ((handle)->Instance->ARR = (value))
#define __HAL_TIM_SET_COMPARE(handle, channel, value) \
    ((void)(channel), (handle)->Instance->CCR1 = (value))

extern "C" {
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef* handle, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef* handle, uint32_t channel);
void fakeDisableIrq();
void fakeEnableIrq();
}

#define __disable_irq() fakeDisableIrq()
#define __enable_irq() fakeEnableIrq()

#endif  // WS2812_TEST_FAKE_TIM_H
