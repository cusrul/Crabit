#include "led_rail_stop2.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Port {
    uint8_t reg = 0xFF;
    unsigned calls = 0, failAt = 0;
    bool low = false, restored = false, powerTried = false, settled = false;
    std::vector<uint8_t> writes;
    bool good() { return ++calls != failAt; }
    bool readEnable(uint8_t& value) {
        if (!good())
            return false;
        value = reg;
        return true;
    }
    bool writeEnable(uint8_t value) {
        assert(low);
        powerTried = true;
        if (!good())
            return false;
        assert(((value ^ reg) & ~2u) == 0);  // Never modify any other rail.
        writes.push_back(value);
        reg = value;
        return true;
    }
    void holdDataLow() { low = true; }
    void settle() {
        assert(reg & 2);
        settled = true;
    }
    void restoreDataPin() {
        assert(settled && (reg & 2));
        low = false;
        restored = true;
    }
};
int main() {
    for (unsigned original = 0; original < 256; ++original) {
        Port p;
        p.reg = original;
        LedRailStop2::Session<Port> s(p);
        assert(s.prepare());
        assert(p.low && p.reg == (original & ~2u));
        assert(s.restore());
        assert(p.reg == original);
        assert(p.restored == bool(original & 2));
        assert(p.low == !bool(original & 2));
    }
    for (unsigned failure = 1; failure <= 3; ++failure) {
        Port p;
        p.failAt = failure;
        LedRailStop2::Session<Port> s(p);
        assert(!s.prepare());
        assert(s.restore());
        assert(p.reg == 0xFF);
        if (failure == 1)
            assert(!p.low && !p.powerTried);
    }
    for (unsigned failure = 1; failure <= 3; ++failure) {
        Port p;
        LedRailStop2::Session<Port> s(p);
        assert(s.prepare());
        p.failAt = p.calls + failure;
        assert(!s.restore());
        assert(p.low && !p.restored);
    }
    {
        Port p;
        LedRailStop2::Session<Port> s(p);
        assert(s.prepare());
        p.reg ^= 0x80;
        assert(s.restore());
        assert(p.reg == 0x7F);
    }
    puts("PASS: all256 rail states, ALDO2-only writes, DIN-low before power-off,");
    puts(
        "partial entry unwind, restore failures retain DIN-low, already-off stays off, other-bit "
        "preservation");
}
