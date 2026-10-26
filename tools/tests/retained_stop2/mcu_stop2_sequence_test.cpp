#include "mcu_stop2_sequence.h"
#include "wake_diag_format.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>
using McuStop2::Reason;
struct Port {
    Reason first = Reason::None, second = Reason::None, wake = Reason::Pa6;
    bool paused = false, halted = false, asleep = false, off = false, hardwareReady = true;
    bool ready = true, keys = true, waited = false, sensors = true, restored = true, motion = false;
    std::vector<int> steps;
    Reason blocked() {
        steps.push_back(0);
        return first;
    }
    void displaySleep() {
        assert(!paused);
        off = true;
        steps.push_back(1);
    }
    void pause() {
        assert(off);
        paused = true;
        steps.push_back(2);
    }
    Reason blockedWhilePaused() {
        assert(paused);
        steps.push_back(3);
        return second;
    }
    bool prepareSensors() {
        assert(paused && hardwareReady);
        steps.push_back(11);
        return sensors;
    }
    bool restoreSensors() {
        assert(paused && hardwareReady);
        steps.push_back(12);
        return restored;
    }
    bool motionPending() const { return motion; }
    bool quiesce() {
        assert(paused && !halted);
        hardwareReady = false;
        steps.push_back(4);
        return ready;
    }
    void armAndStopTicks() {
        assert(paused && !hardwareReady);
        halted = true;
        steps.push_back(5);
    }
    bool buttonsReleased() const { return keys; }
    McuStop2::Result wait() {
        assert(halted && paused && off && !hardwareReady);
        waited = true;
        steps.push_back(6);
        return {wake, true, 12, 0};
    }
    void restoreClock(McuStop2::Result& r) {
        assert(halted && paused);
        halted = false;
        r.hclk = 80000000;
        steps.push_back(7);
    }
    void restorePeripherals() {
        assert(paused && !halted);
        hardwareReady = true;
        steps.push_back(8);
    }
    void resume() {
        assert(paused && hardwareReady && !halted);
        paused = false;
        steps.push_back(9);
    }
    void displayWake() {
        assert(!paused && hardwareReady);
        off = false;
        steps.push_back(10);
    }
};
int main() {
    for (Reason reason :
         {Reason::Usb, Reason::Radio, Reason::Busy, Reason::I2cFault, Reason::Keys}) {
        Port p;
        p.first = reason;
        const auto r = McuStop2::run(p);
        assert(r.reason == reason && !r.waited && p.steps == std::vector<int>{0});
    }
    {
        Port p;
        const auto r = McuStop2::run(p);
        assert(r.waited && r.reason == Reason::Pa6 && r.seconds == 12 && r.hclk == 80000000);
        assert((p.steps == std::vector<int>{0, 1, 2, 3, 11, 4, 5, 6, 7, 8, 12, 9, 10}));
    }
    {
        Port p;
        p.second = Reason::Usb;
        const auto r = McuStop2::run(p);
        assert(!r.waited && r.reason == Reason::Usb);
        assert((p.steps == std::vector<int>{0, 1, 2, 3, 9, 10}));
    }
    {
        Port p;
        p.ready = false;
        const auto r = McuStop2::run(p);
        assert(!r.waited && r.reason == Reason::InitFailed && p.hardwareReady && !p.paused);
        assert((p.steps == std::vector<int>{0, 1, 2, 3, 11, 4, 5, 7, 8, 12, 9, 10}));
    }
    {
        Port p;
        p.keys = false;
        const auto r = McuStop2::run(p);
        assert(!r.waited && r.reason == Reason::Keys && !p.off && p.hardwareReady);
    }
    {
        Port p;
        p.wake = Reason::Other;
        const auto r = McuStop2::run(p);
        assert(r.waited && r.reason == Reason::Other && !p.off && !p.paused);
    }
    {
        Port p;
        p.sensors = false;
        const auto r = McuStop2::run(p);
        assert(!r.waited && r.reason == Reason::SensorFailed);
        assert((p.steps == std::vector<int>{0, 1, 2, 3, 11, 12, 9, 10}));
    }
    {
        Port p;
        p.restored = false;
        const auto r = McuStop2::run(p);
        assert(r.waited && r.reason == Reason::SensorFailed && !p.paused);
    }
    {
        Port p;
        p.motion = true;
        const auto r = McuStop2::run(p);
        assert(!r.waited && r.reason == Reason::MotionPending && !p.paused);
        assert(p.off && p.hardwareReady && !p.halted);
        assert((p.steps == std::vector<int>{0, 1, 2, 3, 11, 4, 5, 7, 8, 12, 9}));
        // A retry with motion cleared still enters STOP2 and wakes normally.
        p.motion = false;
        p.steps.clear();
        const auto retry = McuStop2::run(p);
        assert(retry.waited && !p.off && !p.paused && p.hardwareReady);
    }
    {
        Port p;
        p.wake = Reason::Qmi;
        const auto r = McuStop2::run(p);
        assert(r.waited && r.reason == Reason::Qmi);
        assert(!p.off && p.hardwareReady && !p.paused);
    }
    char text[32];
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::Wake, 1, 6, 12));
    assert(strcmp(text, "1/P6/12s") == 0);
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::Samples, 1, 1, 80000000));
    assert(strcmp(text, "Q1 P1 80M") == 0);
    assert(WakeDiag::format(text, sizeof text, WakeDiag::Row::Wake, UINT32_MAX, 7, UINT32_MAX));
    puts("PASS: five entry guards, full sequence, late USB/key abort, init failure unwind,");
    puts(
        "OTHER not mislabeled PA6, clock/peripherals before task resume, display after resume, "
        "formats");
}
