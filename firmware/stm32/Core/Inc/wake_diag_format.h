#pragma once

#include <cstdio>
#include <cstdint>

namespace WakeDiag {
enum class Row {
    Clock,
    UiWait,
    Sensor,
    I2c,
    Usb,
    Fault,
    Lines,
    Recovery,
    WaitSensor,
    Wake,
    Samples
};

// Formats RAM snapshots only. No HAL calls, register reads, allocation or locks.
inline bool format(char* out, size_t capacity, Row row, uint32_t first, uint32_t second = 0,
                   uint32_t third = 0) {
    int length = -1;
    const unsigned long a = static_cast<unsigned long>(first);
    const unsigned long b = static_cast<unsigned long>(second);
    switch (row) {
        case Row::Clock:
            length = snprintf(out, capacity, "%lu MHz", a / 1000000UL);
            break;
        case Row::UiWait:
            length = snprintf(out, capacity, "%lums/%lu", a, b);
            break;
        case Row::Sensor:
            length = snprintf(out, capacity, "%lums", a);
            break;
        case Row::I2c:
            length = snprintf(out, capacity, "%lu/0x%lX", a, b);
            break;
        case Row::Fault: {
            const char* device = first == 1   ? "QMI"
                                 : first == 2 ? "BAT"
                                 : first == 3 ? "PAT"
                                 : first == 4 ? "BMP"
                                 : first == 5 ? "AXP"
                                              : "BUS";
            length = snprintf(out, capacity, "%s:%02lX", device, b);
            break;
        }
        case Row::Lines:
            length = snprintf(out, capacity, "%u%u E%lX", (first >> 1) & 1u, first & 1u, b);
            break;
        case Row::Recovery: {
            const char* state = second == 1   ? "IDLE"
                                : second == 2 ? "FREE"
                                : second == 3 ? "SCL0"
                                : second == 4 ? "SDA0"
                                : second == 5 ? "PROBE"
                                : second == 6 ? "INIT"
                                              : "NONE";
            length = snprintf(out, capacity, "%lu/%s", a, state);
            break;
        }
        case Row::WaitSensor:
            length = snprintf(out, capacity, "%lu/%lums", a, b);
            break;
        case Row::Wake: {
            const char* reason = second == 1    ? "USB"
                                 : second == 2  ? "RAD"
                                 : second == 3  ? "KEY"
                                 : second == 4  ? "BUSY"
                                 : second == 5  ? "I2C"
                                 : second == 6  ? "P6"
                                 : second == 7  ? "OTHER"
                                 : second == 8  ? "INIT"
                                 : second == 9  ? "QMI"
                                 : second == 10 ? "SENS"
                                 : second == 11 ? "MOVE"
                                                : "RUN";
            length = snprintf(out, capacity, "%lu/%s/%lus", a, reason,
                              static_cast<unsigned long>(third));
            break;
        }
        case Row::Samples:
            length = snprintf(out, capacity, "Q%u P%u %luM", first ? 1u : 0u, second ? 1u : 0u,
                              static_cast<unsigned long>(third / 1000000u));
            break;
        case Row::Usb: {
            const char* state = first == 1   ? "DEF"
                                : first == 2 ? "ADDR"
                                : first == 3 ? "CFG"
                                : first == 4 ? "SUSP"
                                             : "NONE";
            length = snprintf(out, capacity, "%s/%lu", state, b);
            break;
        }
    }
    return length >= 0 && static_cast<size_t>(length) < capacity;
}
}  // namespace WakeDiag
