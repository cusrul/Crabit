#pragma once
// QMI8658 run profile and the awake-only WoM probe session. Both are pure
// configuration/state; the caller owns the I2C bus.

#include <stdint.h>
#include <array>
#include <atomic>

class QmiRunProfile {
public:
    static inline constexpr unsigned SampleHz = 250;
    static inline constexpr uint8_t Ctrl2 = 0x25;  // +/-8g, accel-only 250Hz, 4096 LSB/g.
    static constexpr unsigned samples(unsigned milliseconds) {
        return (milliseconds * SampleHz + 500u) / 1000u;
    }
    // Preserve the OLD actual 1000Hz windows: 36ms peak, 90ms quiet, 448ms double.
    // samples(36)=9, samples(90)=23, samples(448)=112 at 250Hz.
    static inline constexpr std::array<uint8_t, 8> TapTiming = {
        9, 4, 23 & 0xFF, 23 >> 8, 112 & 0xFF, 112 >> 8, 0, 1};
    // At a four-times longer sampling interval, preserve approximate smoothing:
    // new gain = 1 - (1 - old gain)^4, quantized to 1/128.
    // Alpha8/128 ->29/128; Gamma32/128 ->88/128. Physical thresholds unchanged.
    static inline constexpr std::array<uint8_t, 8> TapThreshold = {29, 88, 0x20, 3, 0x90, 1, 0, 2};
    static_assert(SampleHz > 200, "QST recommends Tap ODR above 200Hz");
};

// Temporary awake-only probe. The UI owns the I2C mutex for each operation.
// SensorHub must recheck active after taking that mutex, before any sensor read.
class QmiRunProbe {
public:
    static inline std::atomic<bool> active{false};
    template <class Bus>
    class Session {
        Bus& bus;
        uint16_t address;
        uint8_t saved[6]{};
        bool savedOk = false, womAttempted = false;
        bool set(uint8_t reg, uint8_t value) {
            uint8_t actual = 0;
            return bus.write(address, reg, value) && bus.read(address, reg, actual) && actual == value;
        }
        bool command() {
            if (!bus.write(address, 0x0A, 8))
                return false;
            bool done = false;
            for (unsigned i = 0; i < 50; ++i) {
                uint8_t status = 0;
                if (!bus.read(address, 0x2D, status))
                    break;
                if (status & 0x80) {
                    done = true;
                    break;
                }
                bus.delay();
            }
            if (!bus.write(address, 0x0A, 0) || !done)
                return false;
            for (unsigned i = 0; i < 50; ++i) {
                uint8_t status = 0;
                if (!bus.read(address, 0x2D, status))
                    return false;
                if (!(status & 0x80))
                    return true;
                bus.delay();
            }
            return false;
        }

       public:
        Session(Bus& b, uint16_t a) : bus(b), address(a) {}
        void reset(uint16_t a) {
            address = a;
            savedOk = womAttempted = false;
        }
        bool prepare() {
            if (!address)
                return false;
            constexpr uint8_t regs[] = {2, 3, 8, 9, 0x0B, 0x0C};
            for (unsigned i = 0; i < 6; ++i)
                if (!bus.read(address, regs[i], saved[i]))
                    return false;
            savedOk = true;
            if (!set(8, 0) || !set(2, uint8_t((saved[0] & ~1u) | 8u)) || !set(9, 0xC0) || !set(3, 0x2F))
                return false;
            womAttempted = true;
            if (!set(0x0B, 128) || !set(0x0C, 1) || !command())
                return false;
            bus.armMotion();
            return set(8, 0x21);
        }
        bool restore() {
            if (!savedOk)
                return true;
            bool ok = set(2, uint8_t(saved[0] & ~1u));
            ok = set(8, 0) && ok;
            if (womAttempted) {
                ok = set(9, 0xC0) && ok;
                ok = set(0x0B, 0) && ok;
                ok = set(0x0C, 0) && ok;
                ok = command() && ok;
            }
            // CAL is a shared command parameter window. Restore the bytes AFTER
            // committing WoM disable, without issuing another command using them.
            ok = set(0x0B, saved[4]) && ok;
            ok = set(0x0C, saved[5]) && ok;
            ok = set(3, saved[1]) && ok;
            ok = set(9, saved[3]) && ok;
            ok = set(2, saved[0]) && ok;
            ok = set(8, saved[2]) && ok;
            return ok;
        }
    };
};
