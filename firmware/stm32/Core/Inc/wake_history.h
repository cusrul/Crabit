#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

// UI task is the sole owner of both recording and USB reads. No I/O, clocks,
// allocation, sensor reads or interrupt manipulation occurs while recording.
namespace WakeHistory {
enum Event : uint32_t { ScreenOff = 1, StopResult = 2, Confirm = 3, Key = 4, Guard = 5,
    CoreBefore = 6, CoreAfter = 7, PendingBefore = 8, PendingAfter = 9,
    EnabledBefore = 10, EnabledAfter = 11, ExtiPowerBefore = 12, ExtiPowerAfter = 13 };
struct Entry { uint32_t sequence, tick, event, a, b, c, d; };
constexpr size_t Capacity = 48;
inline Entry ring[Capacity]{}, snapshot[Capacity]{};
inline uint32_t nextSequence = 0, dropped = 0;
inline size_t head = 0, count = 0, snapshotCount = 0;
inline uint32_t snapshotDropped = 0;
inline void record(uint32_t tick, Event event, uint32_t a = 0, uint32_t b = 0,
                   uint32_t c = 0, uint32_t d = 0) {
    ring[head] = {++nextSequence, tick, uint32_t(event), a, b, c, d};
    head = (head + 1) % Capacity;
    if (count < Capacity) ++count; else ++dropped;
}
inline size_t format(char* out, size_t capacity, uint32_t index) {
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
}  // namespace WakeHistory
