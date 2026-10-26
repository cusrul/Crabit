#include "sensor_stop2.h"
#include "qmi_run_profile.h"
#include <cassert>
#include <cstdio>
#include <cstring>
struct Bus {
    uint8_t q[256]{}, p[256]{};
    unsigned operations = 0, failAt = 0, delays = 0;
    bool armed = false, stuck = false;
    Bus() {
        q[2] = 0x5E;
        q[3] = QmiRunProfile::Ctrl2;
        q[8] = 0x21;
        q[9] = 0xC1;
        p[6] = 0x17;
    }
    bool good() { return ++operations != failAt; }
    bool read(uint16_t a, uint8_t r, uint8_t& value) {
        if (!good())
            return false;
        value = a == 0xD4 ? q[r] : p[r];
        return true;
    }
    bool write(uint16_t a, uint8_t r, uint8_t value) {
        if (!good())
            return false;
        auto* regs = a == 0xD4 ? q : p;
        regs[r] = value;
        if (a == 0xD4 && r == 0x0A)
            q[0x2D] = value && !stuck ? 0x80 : 0;
        return true;
    }
    void delay() { ++delays; }
    void armMotion() {
        assert(q[8] == 0 && q[0x0B] == 192);
        armed = true;
    }
    void checkRestored() {
        assert(q[2] == 0x5E && q[3] == QmiRunProfile::Ctrl2 && q[8] == 0x21 && q[9] == 0xC1);
        assert(p[6] == 0x17 && p[9] == 0);
    }
};
int main() {
    unsigned count = 0, offCount = 0;
    {
        Bus b;
        SensorStop2::Session<Bus> s(b, 0xD4, 0xEA);
        assert(s.prepare(true));
        count = b.operations;
        assert(b.armed && b.q[3] == 0x2F && b.q[8] == 0x21 && b.q[9] == 0xC0);
        assert(b.p[6] == 0x1F && b.q[0x0B] == 192 && b.q[0x0C] == 1);
        assert(s.restore());
        b.checkRestored();
        assert(b.q[0x0B] == 0);
    }
    {
        Bus b;
        SensorStop2::Session<Bus> s(b, 0xD4, 0xEA);
        assert(s.prepare(false));
        offCount = b.operations;
        assert(!b.armed && b.q[8] == 0 && b.q[2] == 0x5F);
        assert(s.restore());
        b.checkRestored();
    }
    for (unsigned i = 1; i <= count; ++i) {
        Bus b;
        b.failAt = i;
        SensorStop2::Session<Bus> s(b, 0xD4, 0xEA);
        assert(!s.prepare(true));
        assert(s.restore());
        b.checkRestored();
    }
    for (unsigned i = 1; i <= offCount; ++i) {
        Bus b;
        b.failAt = i;
        SensorStop2::Session<Bus> s(b, 0xD4, 0xEA);
        assert(!s.prepare(false));
        assert(s.restore());
        b.checkRestored();
    }
    {
        Bus b;
        b.stuck = true;
        SensorStop2::Session<Bus> s(b, 0xD4, 0xEA);
        assert(!s.prepare(true));
        assert(b.delays == 50);
        assert(!s.restore());
    }
    {
        Bus b;
        SensorStop2::Session<Bus> s(b, 0xD4, 0xEA);
        assert(s.prepare(true));
        b.failAt = b.operations + 1;
        assert(!s.restore());
    }
    {
        Bus b;
        SensorStop2::Session<Bus> s(b, 0, 0xEA);
        assert(!s.prepare(true) && b.operations == 0);
        assert(s.restore());
    }
    printf(
        "PASS: WoM/powerdown, saved-register restoration, %u WoM + %u powerdown fault injections, "
        "timeout, restore failure, absent sensor\n",
        count, offCount);
}
