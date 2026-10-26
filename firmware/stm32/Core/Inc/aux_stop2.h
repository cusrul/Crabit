#pragma once
#include <stdint.h>
#include <atomic>

namespace AuxStop2 {
inline std::atomic<bool> restoreFault{false};
// Caller owns I2C, tasks are paused, HAL timeouts still run. No rail/reset writes.
template <class Bus>
class Session {
    Bus& bus;
    uint16_t bmp, pmic;
    uint8_t control = 0, adc = 0;
    bool savedBmp = false, savedAdc = false;
    bool set(uint16_t addr, uint8_t reg, uint8_t value) {
        uint8_t actual = 0;
        return bus.write(addr, reg, value) && bus.read(addr, reg, actual) && actual == value;
    }

   public:
    Session(Bus& b, uint16_t ba, uint16_t pa) : bus(b), bmp(ba), pmic(pa) {}
    bool prepare() {
        if (bmp) {
            if (!bus.read(bmp, 0xF4, control))
                return false;
            // Reject one-shot modes: restoring one would retrigger a measurement.
            if ((control & 3) == 1 || (control & 3) == 2)
                return false;
            savedBmp = true;
            if (!bus.write(bmp, 0xF4, uint8_t(control & ~3u)))
                return false;
            bool sleeping = false;
            for (unsigned i = 0; i < 50; ++i) {
                uint8_t actual = 0, status = 0;
                if (!bus.read(bmp, 0xF4, actual) || !bus.read(bmp, 0xF3, status))
                    return false;
                if (actual == uint8_t(control & ~3u) && !(status & 0x08)) {
                    sleeping = true;
                    break;
                }
                bus.delay();
            }
            if (!sleeping)
                return false;
        }
        if (!bus.read(pmic, 0x30, adc))
            return false;
        savedAdc = true;
        return set(pmic, 0x30, uint8_t(adc & ~0x3Fu));
    }
    bool restore() {
        bool ok = true;
        if (savedAdc)
            ok = set(pmic, 0x30, adc) && ok;
        if (savedBmp)
            ok = set(bmp, 0xF4, control) && ok;
        return ok;
    }
};
}  // namespace AuxStop2
