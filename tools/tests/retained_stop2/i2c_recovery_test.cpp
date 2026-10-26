#include "i2c_recovery_policy.h"
#include "wake_diag_format.h"
#include <cassert>
#include <cstring>
#include <cstdio>
using I2cRecovery::Result;

struct Pins {
    bool clockHigh = true, dataHigh = true, gpio = false, clockDrivenLow = false;
    bool dataDrivenLow = false, cleaned = false;
    unsigned clocks = 0, releaseAfter = 0, stretchAt = 0, lows = 0;
    bool scl() const { return clockHigh && !clockDrivenLow; }
    bool sda() const {
        return (dataHigh || (releaseAfter && clocks >= releaseAfter)) && !dataDrivenLow;
    }
    void gpioOpenDrain() { gpio = true; }
    void pause() {}
    void sclLow() {
        clockDrivenLow = true;
        ++lows;
    }
    void sclRelease() {
        assert(clockDrivenLow);
        clockDrivenLow = false;
        ++clocks;
        if (clocks == stretchAt)
            clockHigh = false;
    }
    bool waitScl() { return scl(); }
    void sdaLow() {
        assert(!scl());
        dataDrivenLow = true;
    }
    void sdaRelease() {
        assert(scl());
        dataDrivenLow = false;
    }
    void releaseOutputs() {
        clockDrivenLow = dataDrivenLow = false;
        cleaned = true;
    }
};

int main() {
    {
        Pins p;
        assert(I2cRecovery::releaseBus(p) == Result::Idle);
        assert(!p.gpio && p.clocks == 0);
    }
    {
        Pins p;
        p.clockHigh = false;
        p.dataHigh = false;
        assert(I2cRecovery::releaseBus(p) == Result::SclLow);
        assert(!p.gpio && !p.cleaned && p.clocks == 0);
    }
    for (unsigned n = 1; n <= 9; ++n) {
        Pins p;
        p.dataHigh = false;
        p.releaseAfter = n;
        assert(I2cRecovery::releaseBus(p) == Result::Released);
        assert(p.cleaned && p.clocks == n + 1 && p.scl() && p.sda());  // +1 STOP sequence
    }
    {
        Pins p;
        p.dataHigh = false;
        assert(I2cRecovery::releaseBus(p) == Result::SdaLow);
        assert(p.cleaned && p.clocks == 9 && !p.clockDrivenLow && !p.dataDrivenLow);
    }
    {
        Pins p;
        p.dataHigh = false;
        p.releaseAfter = 2;
        p.stretchAt = 1;
        assert(I2cRecovery::releaseBus(p) == Result::SclLow);
        assert(p.cleaned && p.clocks == 1);
    }
    {
        Pins p;
        p.dataHigh = false;
        p.releaseAfter = 1;
        p.stretchAt = 2;
        assert(I2cRecovery::releaseBus(p) == Result::SclLow);
        assert(p.cleaned && p.clocks == 2);
    }
    I2cRecovery::Budget retry(2000);
    retry.fault(100);
    assert(!retry.due(2099) && retry.due(2100));
    for (unsigned n = 1; n <= 3; ++n) {
        const uint32_t now = 100 + n * 2000;
        assert(retry.due(now));
        retry.beginAttempt(now);
        assert(retry.attempts() == n);
        assert(retry.consumeClear() == (n == 1));
    }
    assert(retry.held() && !retry.due(100000));
    assert(retry.syncWake(1) && !retry.held() && retry.attempts() == 0);
    retry.fault(0);
    retry.beginAttempt(2000);
    assert(retry.consumeClear());
    retry.probePassed(2000);
    retry.healthy(3999);
    assert(retry.attempts() == 1 && !retry.consumeClear());
    retry.fault(3999);
    assert(!retry.due(5998) && retry.due(5999));
    retry.beginAttempt(5999);
    retry.probePassed(6000);
    retry.healthy(8000);
    assert(retry.attempts() == 0 && retry.consumeClear());
    retry.fault(UINT32_MAX - 1000);
    assert(!retry.due(998) && retry.due(999));
    char text[32];
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::Fault, 1, 0x6A));
    assert(strcmp(text, "QMI:6A") == 0);
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::Lines, 3, 0x20));
    assert(strcmp(text, "11 E20") == 0);
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::Recovery, 3, 3));
    assert(strcmp(text, "3/SCL0") == 0);
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::WaitSensor, UINT32_MAX, UINT32_MAX));
    puts("PASS: idle/low lines, 1..9 pulse release/STOP, stretch abort/cleanup, 3 attempts,");
    puts("one clear per episode, stable verification, wake reset, tick wrap and display bounds");
}
