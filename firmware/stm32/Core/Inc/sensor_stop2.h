#pragma once
#include <stdint.h>
#include <atomic>

// Retained-supply sensor transition. Bus operations are bounded; the caller
// owns I2C and has paused all other users. No reset or rail writes are allowed.
namespace SensorStop2 {
// A failed restoration needs an explicit reboot, not an automatic I2C bus
// recovery that assumes sensor configuration is still known.
inline std::atomic<bool> restoreFault{false};
template <class Bus>
class Session {
    Bus& bus;
    uint16_t qmi, pat;
    uint8_t q[4]{}, p = 0, protect = 0;
    bool savedQ = false, savedP = false, womAttempted = false;
    bool read(uint16_t a, uint8_t r, uint8_t& v) { return bus.read(a, r, v); }
    bool set(uint16_t a, uint8_t r, uint8_t v) {
        uint8_t actual = 0;
        return bus.write(a, r, v) && read(a, r, actual) && actual == v;
    }
    bool command() {
        if (!bus.write(qmi, 0x0A, 0x08))
            return false;
        bool done = false;
        for (unsigned i = 0; i < 50; ++i) {
            uint8_t status = 0;
            if (!read(qmi, 0x2D, status))
                break;
            if (status & 0x80) {
                done = true;
                break;
            }
            bus.delay();
        }
        // ACK even on timeout, so a partial entry can be unwound.
        if (!bus.write(qmi, 0x0A, 0))
            return false;
        if (!done)
            return false;
        for (unsigned i = 0; i < 50; ++i) {
            uint8_t status = 0;
            if (!read(qmi, 0x2D, status))
                return false;
            if (!(status & 0x80))
                return true;
            bus.delay();
        }
        return false;
    }

   public:
    Session(Bus& b, uint16_t qa, uint16_t pa) : bus(b), qmi(qa), pat(pa) {}
    bool prepare(bool motion) {
        // Unknown/missing sensors block this experimental deep-sleep node.
        if (!qmi || !pat)
            return false;
        if (!read(qmi, 0x02, q[0]) || !read(qmi, 0x03, q[1]) || !read(qmi, 0x08, q[2]) ||
            !read(qmi, 0x09, q[3]))
            return false;
        savedQ = true;
        if (!read(pat, 0x06, p) || !read(pat, 0x09, protect))
            return false;
        savedP = true;
        if (!set(pat, 0x09, 0x5A) || !set(pat, 0x06, uint8_t(p | 0x08)))
            return false;
        if (!set(qmi, 0x08, 0))
            return false;
        if (!motion)
            return set(qmi, 0x02, uint8_t(q[0] | 1));
        if (!set(qmi, 0x02, uint8_t((q[0] & ~1u) | 0x08)) || !set(qmi, 0x09, 0xC0) ||
            !set(qmi, 0x03, 0x2F))
            return false;
        // QST WoM: 192mg threshold, INT1 initially low, one-sample blanking.
        // 3Hz is a deliberate first hardware-test tradeoff, not precision lift detection.
        womAttempted = true;
        if (!set(qmi, 0x0B, 192) || !set(qmi, 0x0C, 1) || !command())
            return false;
        bus.armMotion();              // Clear old Tap edges before enabling new WoM detection.
        return set(qmi, 0x08, 0x21);  // Accel only; no DRDY on INT2.
    }
    bool restore() {
        bool ok = true;
        if (savedQ) {
            // Restore all saved registers even if a prior operation fails.
            ok = set(qmi, 0x02, uint8_t(q[0] & ~1u)) && ok;
            ok = set(qmi, 0x08, 0) && ok;
            if (womAttempted) {
                ok = set(qmi, 0x09, 0xC0) && ok;
                ok = set(qmi, 0x0B, 0) && ok;
                ok = set(qmi, 0x0C, 0) && ok;
                ok = command() && ok;
            }
            ok = set(qmi, 0x03, q[1]) && ok;
            ok = set(qmi, 0x09, q[3]) && ok;
            ok = set(qmi, 0x02, q[0]) && ok;
            ok = set(qmi, 0x08, q[2]) && ok;
            uint8_t status = 0;
            ok = read(qmi, 0x2F, status) && ok;
        }
        if (savedP) {
            ok = set(pat, 0x09, 0x5A) && ok;
            ok = set(pat, 0x06, p) && ok;
            ok = set(pat, 0x09, protect) && ok;
        }
        return ok;
    }
};
}  // namespace SensorStop2
