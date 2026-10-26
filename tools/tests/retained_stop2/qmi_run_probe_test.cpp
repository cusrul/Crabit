#include "qmi_run_probe.h"
#include <cassert>
#include <cstdio>
struct Bus {
    uint8_t regs[256]{};
    unsigned operations = 0, failAt = 0, statusReads = 0, delays = 0;
    bool armed = false, stuck = false;
    Bus() {
        regs[2] = 0x5E;
        regs[3] = 0x25;
        regs[8] = 0x21;
        regs[9] = 0xC1;
        regs[11] = 0x32;
        regs[12] = 0x41;
    }
    bool read(uint16_t a, uint8_t r, uint8_t& v) {
        assert(a == 0xD4);
        if (r == 0x2F)
            ++statusReads;
        if (++operations == failAt)
            return false;
        v = regs[r];
        return true;
    }
    bool write(uint16_t a, uint8_t r, uint8_t v) {
        assert(a == 0xD4);
        if (++operations == failAt)
            return false;
        assert(r == 2 || r == 3 || r == 8 || r == 9 || r == 10 || r == 11 || r == 12);
        regs[r] = v;
        if (r == 10)
            regs[0x2D] = v && !stuck ? 128 : 0;
        return true;
    }
    void delay() { ++delays; }
    void armMotion() {
        assert(regs[8] == 0 && regs[11] == 128 && regs[12] == 1);
        armed = true;
    }
    void restored() {
        assert(regs[2] == 0x5E && regs[3] == 0x25 && regs[8] == 0x21 && regs[9] == 0xC1);
        assert(regs[11] == 0x32 && regs[12] == 0x41);
    }
};
int main() {
    unsigned operations = 0;
    {
        Bus b;
        QmiRunProbe::Session<Bus> s(b, 0);
        assert(!s.prepare() && b.operations == 0);
        s.reset(0xD4);
        assert(s.prepare());
        operations = b.operations;
        assert(b.armed && b.regs[3] == 0x2F && b.regs[8] == 0x21 && b.regs[9] == 0xC0);
        assert(b.regs[11] == 128 && b.regs[12] == 1 && b.statusReads == 0);
        assert(s.restore());
        b.restored();
        assert(b.statusReads == 0);
        s.reset(0xD4);
        assert(s.prepare());
        assert(s.restore());
        b.restored();
    }
    for (unsigned i = 1; i <= operations; ++i) {
        Bus b;
        b.failAt = i;
        QmiRunProbe::Session<Bus> s(b, 0xD4);
        assert(!s.prepare());
        assert(s.restore());
        b.restored();
        assert(b.statusReads == 0);
    }
    {
        Bus b;
        b.stuck = true;
        QmiRunProbe::Session<Bus> s(b, 0xD4);
        assert(!s.prepare() && b.delays == 50);
        assert(!s.restore());
    }
    puts(
        "PASS: unchanged WoM parameters, QMI-only writes, no status clear, partial unwind, repeat "
        "and timeout");
}
