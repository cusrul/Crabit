#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "hw_ws2812.h"

#define REQUIRE(condition)                                                                \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "requirement failed at %s:%d: %s\n", __FILE__, __LINE__, \
                         #condition);                                                     \
            std::abort();                                                                 \
        }                                                                                 \
    } while (false)

#if defined(WS2812_HARDWARE_TEST)

FakeTimer fakeTimer{};
FakeRcc fakeRcc{};
FakeGpio fakeGpio{};
TIM_HandleTypeDef htim2{};
HAL_StatusTypeDef fakeStartStatus = HAL_ERROR;
HAL_StatusTypeDef fakeStopStatus = HAL_BUSY;
uint32_t fakeStartCalls = 0;
uint32_t fakeStopCalls = 0;
uint32_t fakeStartChannel = 0;
uint32_t fakeStopChannel = 0;
bool fakeStartSawRunning = false;
uint32_t fakeDisableIrqCalls = 0;
uint32_t fakeEnableIrqCalls = 0;
bool fakeInterruptsEnabled = true;

extern "C" HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef* handle, uint32_t channel) {
    ++fakeStartCalls;
    fakeStartChannel = channel;
    fakeStartSawRunning = (handle->Instance->CR1.value & TIM_CR1_CEN) != 0 &&
                          (handle->Instance->CCER.value & TIM_CCER_CC1E) != 0;
    return fakeStartStatus;
}

extern "C" HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef*, uint32_t channel) {
    ++fakeStopCalls;
    fakeStopChannel = channel;
    return fakeStopStatus;
}

extern "C" void fakeDisableIrq() {
    ++fakeDisableIrqCalls;
    fakeInterruptsEnabled = false;
}

extern "C" void fakeEnableIrq() {
    ++fakeEnableIrqCalls;
    fakeInterruptsEnabled = true;
}

struct RunResult {
    WS2812_Driver::Diagnostics diagnostics{};
    std::array<uint32_t, 50> compares{};
};

void resetFakeHardware(uint32_t pinMode, uint32_t pinAf, uint32_t pinType, bool clockEnabled,
                       HAL_StatusTypeDef startStatus, HAL_StatusTypeDef stopStatus) {
    fakeTimer = FakeTimer{};
    fakeRcc = FakeRcc{};
    fakeGpio = FakeGpio{};
    htim2 = TIM_HandleTypeDef{};
    fakeTimer.SR.timer = &fakeTimer;
    htim2.Instance = &fakeTimer;

    fakeTimer.CR1.value = TIM_CR1_CEN | 0x80u;
    fakeTimer.CCER.value = TIM_CCER_CC1E | 0x10u;
    fakeTimer.CCMR1.value = 0x6868u;
    fakeTimer.PSC.value = 0;
    fakeTimer.ARR.value = 73;
    fakeTimer.CNT.value = 23;
    fakeRcc.APB1ENR1.value = clockEnabled ? RCC_APB1ENR1_TIM2EN : 0;
    fakeGpio.MODER.value = pinMode << 10;
    fakeGpio.AFR[0].value = pinAf << 20;
    fakeGpio.OTYPER.value = pinType << 5;

    fakeStartStatus = startStatus;
    fakeStopStatus = stopStatus;
    fakeStartCalls = 0;
    fakeStopCalls = 0;
    fakeStartChannel = 0;
    fakeStopChannel = 0;
    fakeStartSawRunning = false;
    fakeDisableIrqCalls = 0;
    fakeEnableIrqCalls = 0;
    fakeInterruptsEnabled = true;
}

void checkBitstream(const FakeCompareRegister& compare) {
    constexpr std::array<uint8_t, 6> grbBytes = {0x00, 0xFF, 0x00, 0x00, 0x00, 0x00};
    REQUIRE(compare.writes == 50);
    REQUIRE(compare.written_values[0] == 0);
    REQUIRE(compare.written_values[49] == 0);
    for (size_t byteIndex = 0; byteIndex < grbBytes.size(); ++byteIndex) {
        for (size_t bitIndex = 0; bitIndex < 8; ++bitIndex) {
            const size_t traceIndex = 1 + byteIndex * 8 + bitIndex;
            const bool one = (grbBytes[byteIndex] & (1u << (7 - bitIndex))) != 0;
            REQUIRE(compare.written_values[traceIndex] == (one ? 56u : 28u));
        }
    }
    REQUIRE(compare.written_values[1] == 28);
    REQUIRE(compare.written_values[9] == 56);
    REQUIRE(compare.written_values[16] == 56);
    REQUIRE(compare.written_values[17] == 28);
}

void checkNoDiagnosticSideEffects() {
    REQUIRE(fakeTimer.CR1.writes == 0);
    REQUIRE(fakeTimer.CCER.writes == 0);
    REQUIRE(fakeTimer.CCMR1.writes == 0);
    REQUIRE(fakeTimer.PSC.writes == 0);
    REQUIRE(fakeTimer.ARR.writes == 1);
    REQUIRE(fakeTimer.ARR.value == 99);
    REQUIRE(fakeTimer.CNT.writes == 1);
    REQUIRE(fakeTimer.CNT.reset_written);
    REQUIRE(fakeRcc.APB1ENR1.writes == 0);
    REQUIRE(fakeTimer.SR.writes > 0);
    REQUIRE(fakeGpio.MODER.writes == 0);
    REQUIRE(fakeGpio.AFR[0].writes == 0);
    REQUIRE(fakeGpio.AFR[1].writes == 0);
    REQUIRE(fakeGpio.OTYPER.writes == 0);
}

RunResult runHardwareShow(bool withDiagnostics, uint32_t pinMode, uint32_t pinAf, uint32_t pinType,
                          bool clockEnabled = true, HAL_StatusTypeDef startStatus = HAL_ERROR,
                          HAL_StatusTypeDef stopStatus = HAL_BUSY) {
    resetFakeHardware(pinMode, pinAf, pinType, clockEnabled, startStatus, stopStatus);
    WS2812_Driver driver;
    driver.setBrightness(100);
    driver.setLED(0, 255, 0, 0);

    RunResult result{};
    if (withDiagnostics) {
        driver.show(&result.diagnostics);
    } else {
        driver.show();
    }

    REQUIRE(fakeStartCalls == 1);
    REQUIRE(fakeStopCalls == 1);
    REQUIRE(fakeStartChannel == TIM_CHANNEL_1);
    REQUIRE(fakeStopChannel == TIM_CHANNEL_1);
    REQUIRE(fakeStartSawRunning);
    REQUIRE(fakeDisableIrqCalls == 1);
    REQUIRE(fakeEnableIrqCalls == 1);
    REQUIRE(fakeInterruptsEnabled);
    REQUIRE(fakeTimer.CNT.reads_before_reset == (withDiagnostics ? 2u : 0u));
    REQUIRE(fakeTimer.CNT.reset_written);
    checkNoDiagnosticSideEffects();
    checkBitstream(fakeTimer.CCR1);
    for (size_t i = 0; i < result.compares.size(); ++i) {
        result.compares[i] = fakeTimer.CCR1.written_values[i];
    }
    return result;
}

void checkDiagnostics(const RunResult& result, uint32_t pinMode, uint32_t pinAf, uint32_t pinType,
                      bool clockEnabled, HAL_StatusTypeDef startStatus,
                      HAL_StatusTypeDef stopStatus) {
    const auto& diagnostics = result.diagnostics;
    REQUIRE(diagnostics.supported == 1);
    REQUIRE(diagnostics.start == startStatus);
    REQUIRE(diagnostics.stop == stopStatus);
    REQUIRE(diagnostics.cr1 == (TIM_CR1_CEN | 0x80u));
    REQUIRE(diagnostics.ccer == (TIM_CCER_CC1E | 0x10u));
    REQUIRE(diagnostics.ccmr1 == 0x6868u);
    REQUIRE(diagnostics.psc == 0);
    REQUIRE(diagnostics.arr == 99);
    REQUIRE(diagnostics.cnt_a == 23);
    REQUIRE(diagnostics.cnt_b == 24);
    REQUIRE(diagnostics.clock_enabled == (clockEnabled ? 1u : 0u));
    REQUIRE(diagnostics.pin_mode == pinMode);
    REQUIRE(diagnostics.pin_af == pinAf);
    REQUIRE(diagnostics.pin_type == pinType);
    REQUIRE(diagnostics.brightness == 100);
    REQUIRE(diagnostics.grb == 0xFF00u);
}

int main() {
    const RunResult diagnosed = runHardwareShow(true, 2, 1, 0);
    checkDiagnostics(diagnosed, 2, 1, 0, true, HAL_ERROR, HAL_BUSY);

    const RunResult ordinary = runHardwareShow(false, 2, 1, 0);
    REQUIRE(ordinary.compares == diagnosed.compares);

    const RunResult successfulHal = runHardwareShow(true, 2, 1, 0, true, HAL_OK, HAL_OK);
    checkDiagnostics(successfulHal, 2, 1, 0, true, HAL_OK, HAL_OK);
    REQUIRE(successfulHal.compares == diagnosed.compares);

    const RunResult misconfiguredSnapshot = runHardwareShow(true, 1, 7, 1, false, HAL_OK, HAL_OK);
    checkDiagnostics(misconfiguredSnapshot, 1, 7, 1, false, HAL_OK, HAL_OK);
    REQUIRE(fakeGpio.MODER.value == (1u << 10));
    REQUIRE(fakeGpio.AFR[0].value == (7u << 20));
    REQUIRE(fakeGpio.OTYPER.value == (1u << 5));
    REQUIRE(fakeRcc.APB1ENR1.value == 0);

    std::puts("WS2812 hardware-header diagnostics tests PASS");
    return 0;
}

#else

int main() {
    WS2812_Driver driver;
    driver.setBrightness(100);
    driver.setLED(0, 255, 0, 0);
    driver.show();

    WS2812_Driver::Diagnostics diagnostics;
    driver.show(&diagnostics);
    REQUIRE(diagnostics.supported == 0);
    REQUIRE(diagnostics.start == 0xFFFFFFFFu);
    REQUIRE(diagnostics.stop == 0xFFFFFFFFu);
    REQUIRE(diagnostics.brightness == 100);
    REQUIRE(diagnostics.grb == 0xFF00u);
    std::puts("WS2812 desktop software-only tests PASS");
    return 0;
}

#endif
