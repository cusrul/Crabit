#include <cstdint>
#include <cassert>
#include <cstdio>

// Fake only MCU I/O; execute the actual diagnostic header without the board HAL.
#define __I2C_H__
constexpr unsigned GPIO_PIN_9 = 1u << 9, GPIO_PIN_10 = 1u << 10;
constexpr unsigned GPIO_PIN_SET = 1, HAL_I2C_ERROR_NONE = 0, HAL_I2C_STATE_READY = 1;
constexpr unsigned GPIOA = 0;
struct FakeRegisters {
    uint32_t ISR = 0;
} registers;
struct FakeI2c {
    FakeRegisters* Instance = &registers;
    uint32_t ErrorCode = 0;
} hi2c1;
unsigned state = HAL_I2C_STATE_READY, levels = 3;
unsigned HAL_I2C_GetState(FakeI2c*) {
    return state;
}
unsigned HAL_GPIO_ReadPin(unsigned, unsigned pin) {
    return (levels & (pin == GPIO_PIN_9 ? 2u : 1u)) ? GPIO_PIN_SET : 0;
}
#include "i2c_sample_trace.h"

int main() {
    {
        I2cDiag::Sample trace(I2cDiag::Qmi, 0xD4);
    }
    assert(!I2cDiag::paused.load() && !I2cDiag::latched.load());
    hi2c1.ErrorCode = 0x20;
    registers.ISR = 0x8000;
    levels = 2;
    {
        I2cDiag::Sample trace(I2cDiag::Qmi, 0xD4);
    }
    assert(I2cDiag::paused.load() && I2cDiag::device.load() == I2cDiag::Qmi);
    assert(I2cDiag::address.load() == 0x6A && I2cDiag::lines.load() == 2);
    assert(I2cDiag::isr.load() == 0x8000 && WakeMetrics::last_i2c_error.load() == 0x20);
    hi2c1.ErrorCode = 4;
    levels = 0;
    {
        I2cDiag::Sample trace(I2cDiag::Pat, 0xEA);
    }
    assert(I2cDiag::device.load() == I2cDiag::Qmi && I2cDiag::lines.load() == 2);
    I2cDiag::latched.store(false);
    I2cDiag::paused.store(false);
    hi2c1.ErrorCode = 0;
    state = 2;
    {
        I2cDiag::Sample trace(I2cDiag::Axp, 0x68);
    }
    assert(I2cDiag::paused.load() && I2cDiag::device.load() == I2cDiag::Axp);
    assert(I2cDiag::address.load() == 0x34 && I2cDiag::lines.load() == 0);
    puts("PASS: healthy update, first fault latch, pin/ISR/error/address capture, non-ready state");
}
