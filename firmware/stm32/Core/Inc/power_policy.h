#pragma once
// Pure power-transition policy. No HAL, no peripheral access: the retained
// supply sessions and the STOP2 sequence are host-testable in isolation.

#include <stdint.h>
#include <atomic>

class IdleSleep {
public:
    // The existing deep-sleep duration is retained on disk as an enable switch.
    // The screen timeout is now the sole inactivity deadline; busy guards decide
    // whether to enter STOP2 or remain screen-off without tearing down a page.
    static inline bool due(bool screenOnly, uint32_t screenTimeout, uint32_t deepSetting, uint32_t idle) {
        return !screenOnly && screenTimeout != 0 && deepSetting != 0 && idle >= screenTimeout;
    }
    class WakeInputGuard {
        bool pending = false;
        uint32_t since = 0;

       public:
        void arm(uint32_t now) {
            since = now;
            pending = true;
        }
        bool consume(uint32_t now, bool pressed) {
            if (!pending)
                return false;
            if (!pressed && uint32_t(now - since) >= 200u)
                pending = false;
            return true;  // Also consume the first released sample.
        }
    };
};

// Retained-supply sensor transition. Bus operations are bounded; the caller
// owns I2C and has paused all other users. No reset or rail writes are allowed.
class SensorStop2 {
public:
    // A failed restoration needs an explicit reboot, not an automatic I2C bus
    // recovery that assumes sensor configuration is still known.
    static inline std::atomic<bool> restoreFault{false};
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
};

class AuxStop2 {
public:
    static inline std::atomic<bool> restoreFault{false};
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
};

class LedRailStop2 {
public:
    static inline std::atomic<bool> restoreFault{false};
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
};

class Stop2 {
public:
    enum class Reason : uint32_t {
        None,
        Usb,
        Radio,
        Keys,
        Busy,
        I2cFault,
        Pa6,
        Other,
        InitFailed,
        Qmi,
        SensorFailed,
        MotionPending
    };
    struct Result {
        Reason reason = Reason::None;
        bool waited = false;
        uint32_t seconds = 0, hclk = 0;
    };

    // Defined in stop2.h. Kept out of this header so the pure
    // sequence logic stays testable without the HAL.
    class WatchPort;
    static Result runWatch();

    // Diagnostic whole-system pause, not tickless idle. No elapsed RTOS tick catch-up.
    // Caller must hold the UI's existing I2C mutex throughout; run() does not acquire it.
    // The port suspends task scheduling until clocks, peripherals and sensors are restored.
    template <typename Port>
    static Result run(Port& port) {
        Result result;
        result.reason = port.blocked();
        if (result.reason != Reason::None)
            return result;
        port.displaySleep();
        port.pause();
        result.reason = port.blockedWhilePaused();
        if (result.reason == Reason::None) {
            const bool sensorsReady = port.prepareSensors();
            if (sensorsReady) {
                const bool ready = port.quiesce();
                port.armAndStopTicks();
                if (ready && port.buttonsReleased() && !port.motionPending()) {
                    result = port.wait();
                } else {
                    result.reason = !ready                    ? Reason::InitFailed
                                    : !port.buttonsReleased() ? Reason::Keys
                                                              : Reason::MotionPending;
                }
                port.restoreClock(result);
                port.restorePeripherals();
            } else {
                result.reason = Reason::SensorFailed;
            }
            if (!port.restoreSensors())
                result.reason = Reason::SensorFailed;
        }
        port.resume();
        // Pending motion cancels entry, rather than waking an already sleeping system.
        if (result.waited || result.reason != Reason::MotionPending)
            port.displayWake();
        return result;
    }
};
