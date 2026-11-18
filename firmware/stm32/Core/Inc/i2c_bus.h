#pragma once
#include <stddef.h>
#include <stdint.h>
#include <atomic>

#include "wake_log.h"

// I2C bus, recovery and fault diagnostics. Sensor sampling lives in
// sensor_task.h; this class owns the bus lock and transactions.
class I2cBus {
public:
    static bool init();
    static bool suspend();
    static bool resume();
    static bool ready();
    static uint32_t errorCode();
    static bool lock(uint32_t timeout_ms);
    static void unlock();

    // 8-bit HAL address, one-byte register, same timeout semantics as HAL.
    static bool read(uint8_t address, uint8_t reg, uint8_t* data, size_t length,
                     uint32_t timeout_ms = 10);
    static bool write(uint8_t address, uint8_t reg, const uint8_t* data, size_t length,
                      uint32_t timeout_ms = 10);

    static bool recover();
    static bool recoverySyncWake(uint32_t wake);
    static bool recoveryDue(uint32_t now);
    static void recoveryFault(uint32_t now);
    static void recoveryHealthy(uint32_t now);
    static unsigned recoveryAttempts();
};

void Task_SensorHub(void* argument);

extern "C" {
uint32_t CrabitI2c_Isr(void);
uint32_t CrabitI2c_Error(void);
uint32_t CrabitI2c_LineLevels(void);
bool CrabitI2c_Ready(void);
}

class I2cFault {
public:
    enum Device : uint32_t { Bus, Qmi, Battery, Pat, Bmp, Axp };
    static inline std::atomic<bool> paused{false}, latched{false};
    static inline std::atomic<uint32_t> device{Bus}, address{0}, lines{0}, isr{0}, result{0}, attempts{0};
    static uint32_t lineLevels() { return CrabitI2c_LineLevels(); }
    static void fault(Device source, uint32_t addr) {
        if (!latched.exchange(true, std::memory_order_relaxed)) {
            device.store(source, std::memory_order_relaxed);
            address.store(addr >> 1, std::memory_order_relaxed);
            lines.store(lineLevels(), std::memory_order_relaxed);
            isr.store(CrabitI2c_Isr(), std::memory_order_relaxed);
            WakeMetrics::last_i2c_error.store(CrabitI2c_Error(), std::memory_order_relaxed);
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
            if (CrabitI2c_Error() != 0 || !CrabitI2c_Ready())
                fault(source_, address_);
        }
    };
};
