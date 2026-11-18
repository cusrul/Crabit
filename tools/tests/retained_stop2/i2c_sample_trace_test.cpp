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
extern "C" uint32_t CrabitI2c_Isr(void) { return registers.ISR; }
extern "C" uint32_t CrabitI2c_Error(void) { return hi2c1.ErrorCode; }
extern "C" uint32_t CrabitI2c_LineLevels(void) {
    return (levels & 2u ? 2u : 0u) | (levels & 1u ? 1u : 0u);
}
extern "C" bool CrabitI2c_Ready(void) { return state == HAL_I2C_STATE_READY; }
#include "i2c_bus.h"

int main() {
    {
        I2cFault::Sample trace(I2cFault::Qmi, 0xD4);
    }
    assert(!I2cFault::paused.load() && !I2cFault::latched.load());
    hi2c1.ErrorCode = 0x20;
    registers.ISR = 0x8000;
    levels = 2;
    {
        I2cFault::Sample trace(I2cFault::Qmi, 0xD4);
    }
    assert(I2cFault::paused.load() && I2cFault::device.load() == I2cFault::Qmi);
    assert(I2cFault::address.load() == 0x6A && I2cFault::lines.load() == 2);
    assert(I2cFault::isr.load() == 0x8000 && WakeMetrics::last_i2c_error.load() == 0x20);
    hi2c1.ErrorCode = 4;
    levels = 0;
    {
        I2cFault::Sample trace(I2cFault::Pat, 0xEA);
    }
    assert(I2cFault::device.load() == I2cFault::Qmi && I2cFault::lines.load() == 2);
    I2cFault::latched.store(false);
    I2cFault::paused.store(false);
    hi2c1.ErrorCode = 0;
    state = 2;
    {
        I2cFault::Sample trace(I2cFault::Axp, 0x68);
    }
    assert(I2cFault::paused.load() && I2cFault::device.load() == I2cFault::Axp);
    assert(I2cFault::address.load() == 0x34 && I2cFault::lines.load() == 0);
    puts("PASS: healthy update, first fault latch, pin/ISR/error/address capture, non-ready state");
}
