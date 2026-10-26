#include "aux_stop2.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
struct Bus {
    uint8_t control = 0x27, adc = 0x3D;
    unsigned calls = 0, failAt = 0, delays = 0, measuring = 0;
    bool stuck = false, corruptAdc = false;
    bool good() { return ++calls != failAt; }
    bool read(uint16_t addr, uint8_t reg, uint8_t& value) {
        if (!good())
            return false;
        if (addr == 0xEC && reg == 0xF4)
            value = control;
        else if (addr == 0xEC && reg == 0xF3) {
            value = stuck || measuring ? 8 : 0;
            if (measuring)
                --measuring;
        } else {
            assert(addr == 0x68 && reg == 0x30);
            value = adc;
        }
        return true;
    }
    bool write(uint16_t addr, uint8_t reg, uint8_t value) {
        if (!good())
            return false;
        if (addr == 0xEC) {
            assert(reg == 0xF4);
            control = value;
        } else {
            assert(addr == 0x68 && reg == 0x30);
            adc = corruptAdc ? uint8_t(value ^ 1) : value;
        }
        return true;
    }
    void delay() { ++delays; }
    void check() { assert(control == 0x27 && adc == 0x3D); }
};
int main() {
    unsigned count = 0, restoreCount = 0;
    {
        Bus b;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        b.measuring = 3;
        assert(s.prepare());
        assert(b.delays == 3);
        assert(b.control == 0x24 && b.adc == 0);
        assert(s.restore());
        b.check();
    }
    {
        Bus b;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(s.prepare());
        count = b.calls;
        assert(s.restore());
        restoreCount = b.calls - count;
        b.check();
    }
    for (unsigned i = 1; i <= count; ++i) {
        Bus b;
        b.failAt = i;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(!s.prepare());
        assert(s.restore());
        b.check();
    }
    for (unsigned i = 1; i <= restoreCount; ++i) {
        Bus b;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(s.prepare());
        b.failAt = b.calls + i;
        assert(!s.restore());
        // A failed ADC restore must not suppress the independent BMP restore.
        if (i <= 2)
            assert(b.control == 0x27);
    }
    {
        Bus b;
        b.stuck = true;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(!s.prepare() && b.delays == 50 && b.adc == 0x3D);
        assert(s.restore());
        b.check();
    }
    for (unsigned v = 0; v < 256; ++v) {
        Bus b;
        b.adc = v;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(s.prepare() && b.adc == (v & ~0x3Fu));
        assert(s.restore() && b.adc == v);
    }
    {
        Bus b;
        b.control = 0x24;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(s.prepare());
        assert(s.restore() && b.control == 0x24);
    }
    for (uint8_t oneShot : {uint8_t(0x25), uint8_t(0x26)}) {
        Bus b;
        b.control = oneShot;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(!s.prepare());
        assert(s.restore() && b.control == oneShot && b.calls == 1);
    }
    {
        Bus b;
        AuxStop2::Session<Bus> s(b, 0, 0x68);
        assert(s.prepare() && b.control == 0x27 && b.adc == 0);
        assert(s.restore());
        b.check();
    }
    {
        Bus b;
        b.corruptAdc = true;
        AuxStop2::Session<Bus> s(b, 0xEC, 0x68);
        assert(!s.prepare());
        b.corruptAdc = false;
        assert(s.restore());
        b.check();
    }
    printf(
        "PASS: BMP measuring wait/timeout, sleep and ADC readback, %u entry/%u restore faults, 256 "
        "ADC states, partial unwind, absent BMP, one-shot rejection\n",
        count, restoreCount);
}
