#pragma once
#include <stdint.h>

namespace McuStop2 {
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

// Diagnostic whole-system pause, not tickless idle. No elapsed RTOS tick catch-up.
// Caller must hold the UI's existing I2C mutex throughout; run() does not acquire it.
// The port suspends task scheduling until clocks, peripherals and sensors are restored.
template <typename Port>
Result run(Port& port) {
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
}  // namespace McuStop2
