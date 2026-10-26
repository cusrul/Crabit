#ifndef CRABIT_SNAPSHOT_STORE_H
#define CRABIT_SNAPSHOT_STORE_H

#include <atomic>
#include <stdint.h>

namespace Crabit {

// A short, non-I2C publication guard for small immutable snapshots. Writers and
// readers never share pointers into a live driver object or hold the guard
// across hardware access.
template <typename T>
class SnapshotStore {
private:
    T value{};
    mutable std::atomic_flag guard = ATOMIC_FLAG_INIT;

    void lock() const {
        while (guard.test_and_set(std::memory_order_acquire)) {
        }
    }

    void unlock() const {
        guard.clear(std::memory_order_release);
    }

public:
    void publish(const T& next) {
        lock();
        value = next;
        unlock();
    }

    bool read(T* out) const {
        if (!out) return false;
        if (guard.test_and_set(std::memory_order_acquire)) return false;
        *out = value;
        guard.clear(std::memory_order_release);
        return true;
    }
};

}  // namespace Crabit

#endif
