#pragma once
#include <stdint.h>

// Fail-fast policy: persist a reason in the RTC backup domain and reset.
// The next boot reports it once through diag/boot.
class Fault {
public:
    enum class Reason : uint8_t { None, Boot, I2c, Uart, Usb, Task, HardFault };

    [[noreturn]] static void reset(Reason reason);
    static Reason lastBoot();
};

// Software supervisor. It is stopped by STOP2 because the RTOS is stopped,
// so a long sleep cannot look like a missed heartbeat. IWDG can be added
// later behind the same interface once IWDG_STOP is configured.
class Health {
public:
    enum class Task : uint8_t { Ui, Uart, Sensor, Tx, Count };

    static void init();
    static void start();
    static void beat(Task task);
    static void resume();
    static void check(uint32_t now);
    static uint32_t lastBeat(Task task);

private:
    static constexpr uint32_t TimeoutMs = 3000;
    static constexpr uint32_t GraceMs = 5000;
};
