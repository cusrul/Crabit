#pragma once
#include <stdint.h>
#include <atomic>

namespace LedRailStop2 {
inline std::atomic<bool> restoreFault{false};
// Only ALDO2 (AXP2101 0x90.bit1) belongs to this session. The adapter owns
// PA5/DIN while all LED writers are paused. Never rewrite voltages/other rails.
template <class Port>
class Session {
    Port& port;
    bool saved = false, wasOn = false, held = false;
    static constexpr uint8_t mask = 0x02;

   public:
    explicit Session(Port& p) : port(p) {}
    bool prepare() {
        uint8_t original = 0;
        if (!port.readEnable(original))
            return false;
        saved = true;
        wasOn = (original & mask) != 0;
        port.holdDataLow();
        held = true;
        if (wasOn && !port.writeEnable(uint8_t(original & ~mask)))
            return false;
        uint8_t actual = 0;
        return port.readEnable(actual) && actual == uint8_t(original & ~mask);
    }
    bool restore() {
        if (!saved)
            return true;
        uint8_t current = 0;
        if (!port.readEnable(current))
            return false;
        const uint8_t wanted = uint8_t((current & ~mask) | (wasOn ? mask : 0));
        if (wanted != current && !port.writeEnable(wanted))
            return false;
        uint8_t actual = 0;
        if (!port.readEnable(actual) || actual != wanted)
            return false;
        if (held && wasOn) {
            port.settle();
            port.restoreDataPin();
            held = false;
        }
        // If restoration failed or ALDO2 was already off, leave DIN low.
        return true;
    }
};
}  // namespace LedRailStop2
