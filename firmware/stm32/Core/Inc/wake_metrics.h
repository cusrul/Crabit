#pragma once

#include <atomic>
#include <stdint.h>

// Observation only. Counters live in RAM; no new task, lock, bus access or log.
namespace WakeMetrics {
inline std::atomic<uint32_t> ui_wait_max{0}, ui_lock_misses{0};
inline std::atomic<uint32_t> sensor_max{0}, recoveries{0}, last_i2c_error{0};
inline std::atomic<uint32_t> wakes{0};
inline std::atomic<uint32_t> stop_reason{0}, stop_seconds{0}, stop_hclk{0};

inline void maximum(std::atomic<uint32_t>& value, uint32_t sample) {
    uint32_t previous = value.load(std::memory_order_relaxed);
    while (sample > previous &&
           !value.compare_exchange_weak(previous, sample, std::memory_order_relaxed)) {
    }
}
}  // namespace WakeMetrics
