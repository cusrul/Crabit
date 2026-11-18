#pragma once
// RAM-only wake and STOP2 diagnostics. Nothing here touches a peripheral;
// the UI formats snapshots after the fact.

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <atomic>

// Observation only. Counters live in RAM; no new task, lock, bus access or log.
class WakeMetrics {
public:
    static inline std::atomic<uint32_t> ui_wait_max{0}, ui_lock_misses{0};
    static inline std::atomic<uint32_t> sensor_max{0}, recoveries{0}, last_i2c_error{0};
    static inline std::atomic<uint32_t> wakes{0};
    static inline std::atomic<uint32_t> stop_reason{0}, stop_seconds{0}, stop_hclk{0};

    static inline void maximum(std::atomic<uint32_t>& value, uint32_t sample) {
        uint32_t previous = value.load(std::memory_order_relaxed);
        while (sample > previous &&
               !value.compare_exchange_weak(previous, sample, std::memory_order_relaxed)) {
        }
    }
};

// UI task is the sole owner of both recording and USB reads. No I/O, clocks,
// allocation, sensor reads or interrupt manipulation occurs while recording.
class WakeHistory {
public:
    enum Event : uint32_t { ScreenOff = 1, StopResult = 2, Confirm = 3, Key = 4, Guard = 5,
        CoreBefore = 6, CoreAfter = 7, PendingBefore = 8, PendingAfter = 9,
        EnabledBefore = 10, EnabledAfter = 11, ExtiPowerBefore = 12, ExtiPowerAfter = 13 };
    struct Entry { uint32_t sequence, tick, event, a, b, c, d; };
    static constexpr size_t Capacity = 48;
    static inline Entry ring[Capacity]{}, snapshot[Capacity]{};
    static inline uint32_t nextSequence = 0, dropped = 0;
    static inline size_t head = 0, count = 0, snapshotCount = 0;
    static inline uint32_t snapshotDropped = 0;
    static inline void record(uint32_t tick, Event event, uint32_t a = 0, uint32_t b = 0,
                       uint32_t c = 0, uint32_t d = 0) {
        ring[head] = {++nextSequence, tick, uint32_t(event), a, b, c, d};
        head = (head + 1) % Capacity;
        if (count < Capacity) ++count; else ++dropped;
    }
    static inline size_t format(char* out, size_t capacity, uint32_t index) {
        if (index == 0) {
            snapshotCount = count;
            snapshotDropped = dropped;
            const size_t first = (head + Capacity - count) % Capacity;
            for (size_t i = 0; i < count; ++i) snapshot[i] = ring[(first + i) % Capacity];
        }
        int n;
        if (index >= snapshotCount) {
            n = snprintf(out, capacity, "CRABIT:WAKE %08lX END count=%u lost=%lu\n",
                         static_cast<unsigned long>(index), unsigned(snapshotCount),
                         static_cast<unsigned long>(snapshotDropped));
        } else {
            const auto& e = snapshot[index];
            n = snprintf(out, capacity, "CRABIT:WAKE %08lX s=%lu t=%lu e=%lu a=%08lX b=%08lX c=%08lX d=%08lX\n",
                         static_cast<unsigned long>(index), static_cast<unsigned long>(e.sequence),
                         static_cast<unsigned long>(e.tick), static_cast<unsigned long>(e.event),
                         static_cast<unsigned long>(e.a), static_cast<unsigned long>(e.b),
                         static_cast<unsigned long>(e.c), static_cast<unsigned long>(e.d));
        }
        return n > 0 && static_cast<size_t>(n) < capacity ? static_cast<size_t>(n) : 0;
    }
};

class WakeDiag {
public:
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
    static inline bool format(char* out, size_t capacity, Row row, uint32_t first, uint32_t second = 0,
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
};

// Raw register snapshots bracket the HAL STOP2 call, not the WFI instruction
// itself. ISR execution is still masked; reads must not clear device flags.
class WakeCoreTrace {
public:
    struct Snapshot {
        uint32_t icsr = 0, shcsr = 0, primask = 0, systickValue = 0;
        uint32_t pending[3]{}, enabled[3]{};
        uint32_t exti1 = 0, exti2 = 0, power1 = 0, power2 = 0;
    };
    static Snapshot before, after;
    static bool valid;
};

inline WakeCoreTrace::Snapshot WakeCoreTrace::before{}, WakeCoreTrace::after{};
inline bool WakeCoreTrace::valid = false;

// Written only by the paused STOP2 owner; read by the UI after it resumes.
// STATUS1 is intentionally read only AFTER wake, before WoM is dismantled.
class QmiWakeTrace {
public:
    struct Snapshot {
        uint32_t attempts = 0;
        bool motion = false, prepared = false, statusOk = false, armed = false, returned = false;
        uint8_t regs[6]{};  // CTRL1, CTRL2, CTRL7, CTRL8, CAL1_L, CAL1_H
        uint8_t valid = 0, beforeLevels = 0, afterLevels = 0;
        uint8_t beforePending = 0, afterPending = 0, mask = 0, map = 0, status = 0;
    };
    static Snapshot last;
    template <class Bus>
    static void captureConfig(Bus& bus, uint16_t address) {
        constexpr uint8_t registers[] = {0x02, 0x03, 0x08, 0x09, 0x0B, 0x0C};
        if (!address)
            return;
        for (unsigned i = 0; i < 6; ++i)
            if (bus.read(address, registers[i], last.regs[i]))
                last.valid |= uint8_t(1u << i);
    }
    template <class Bus>
    static void captureStatus(Bus& bus, uint16_t address) {
        if (address && last.prepared && last.motion && last.returned)
            last.statusOk = bus.read(address, 0x2F, last.status);
    }
};

inline QmiWakeTrace::Snapshot QmiWakeTrace::last{};
