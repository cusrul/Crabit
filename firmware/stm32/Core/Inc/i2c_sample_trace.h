#pragma once
#include "i2c.h"
#include "wake_metrics.h"

namespace I2cDiag {
enum Device : uint32_t { Bus, Qmi, Battery, Pat, Bmp, Axp };
inline std::atomic<bool> paused{false}, latched{false};
inline std::atomic<uint32_t> device{Bus}, address{0}, lines{0}, isr{0}, result{0}, attempts{0};
inline uint32_t lineLevels() {
    return (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_9) == GPIO_PIN_SET ? 2u : 0u) |
           (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_10) == GPIO_PIN_SET ? 1u : 0u);
}
inline void fault(Device source, uint32_t addr) {
    if (!latched.exchange(true, std::memory_order_relaxed)) {
        device.store(source, std::memory_order_relaxed);
        address.store(addr >> 1, std::memory_order_relaxed);
        lines.store(lineLevels(), std::memory_order_relaxed);
        isr.store(hi2c1.Instance->ISR, std::memory_order_relaxed);
        WakeMetrics::last_i2c_error.store(hi2c1.ErrorCode, std::memory_order_relaxed);
    }
    paused.store(true, std::memory_order_relaxed);
}
// Checks the final HAL state of each driver update; not every individual transfer.
class Sample {
    Device source_;
    uint32_t address_;

   public:
    Sample(Device source, uint32_t addr) : source_(source), address_(addr) {}
    ~Sample() {
        if (hi2c1.ErrorCode != HAL_I2C_ERROR_NONE ||
            HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY)
            fault(source_, address_);
    }
};
}  // namespace I2cDiag
