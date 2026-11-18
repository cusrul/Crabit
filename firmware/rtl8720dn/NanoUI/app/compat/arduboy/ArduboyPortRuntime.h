#ifndef CRABIT_ARDUBOY_PORT_RUNTIME_H
#define CRABIT_ARDUBOY_PORT_RUNTIME_H

// On-demand runtime storage for the additional Arduboy ports.
//
// Why this file exists: every additional port used to keep its engine (two
// 1,024-byte buffers plus bookkeeping) and its game objects in static RAM.
// Twenty-one engines alone cost about 44 KiB of a 487,424-byte region, which is
// what overflowed the combined link. The installed form of a port is now const
// Flash plus a typed `State` created only while that port is live.
//
// Ownership rules kept from the AppManager design:
//   * AppManager still owns Application objects (launch/delete) and
//     WindowManager still owns Widget/RenderObject trees. This file never
//     deletes either.
//   * The typed State is owned by the Application that created it; its
//     destructor releases it exactly once.
//   * At most one live State per port id. A duplicate launch is refused before
//     AppManager::launch, so a suspended copy cannot share the port.
//   * Saves are independent session buffers retained across exits and owned by
//     this service, never by a transient State.
//
// Allocator: port states and save buffers use the platform allocation pair
// explicitly chosen here, not MemoryPool's global adapter. On the BW16 target
// `ARDUINO` is defined and the Ameba core declares pvPortMalloc/vPortFree
// (the FreeRTOS heap_5 region). Ordinary `operator new` does not pass through
// this runtime's adapter or counters. In the current BW16 split image its
// malloc/free calls are linker-wrapped to pvPortMalloc/vPortFree as well; do
// not infer a separate newlib heap from the library's unresolved malloc call.
// The host build uses libc malloc/free, and host tests inject a counting/failing
// adapter with setAllocator().
// Target limitation: the stock SDK's pvPortMalloc invokes a non-returning
// vApplicationMallocFailedHook on exhaustion. Null-return rollback below is
// verified with the host adapter, not a guarantee of recoverable target OOM.
// Do not replace the SDK hook or global allocator as part of this port adapter.

#include "Arduboy2Compat.h"

#include <new>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace CrabitArduboy {

// Largest catalogue index + 1; save buffers are indexed by the same id.
// Hopper (the full upstream port) reserves id 21 and needs this 22nd slot.
constexpr uint8_t kPortIdCount = 22;

namespace detail {

typedef void *(*AllocateFn)(size_t);
typedef void (*ReleaseFn)(void *);

struct Allocator {
    AllocateFn allocate;
    ReleaseFn release;
};

// Explicit platform pair. Ameba's Arduino.h declares the FreeRTOS allocator and
// maps malloc/free onto it for sketch code; the RTOS pair is named directly so
// the choice is visible and cannot be changed by an unrelated adapter.
inline void *platformAllocate(size_t bytes) {
#if defined(ARDUINO)
    return ::pvPortMalloc(bytes);
#else
    return ::malloc(bytes);
#endif
}

inline void platformRelease(void *pointer) {
#if defined(ARDUINO)
    ::vPortFree(pointer);
#else
    ::free(pointer);
#endif
}

struct Stats {
    uint32_t allocations;
    uint32_t releases;
    uint32_t failures;
    uint32_t live;
    size_t live_bytes;
    size_t peak_bytes;
};

struct SaveSlot {
    uint8_t *bytes;
};

struct Runtime {
    Allocator allocator;
    Stats stats;
    uint32_t admitted;      // one bit per port id
    SaveSlot saves[kPortIdCount];

    Runtime() : allocator{platformAllocate, platformRelease}, stats{},
                admitted(0), saves{} {}
};

inline Runtime &runtime() {
    static Runtime instance;
    return instance;
}

inline void *allocate(size_t bytes) {
    Runtime &rt = runtime();
    void *pointer = rt.allocator.allocate ? rt.allocator.allocate(bytes) : nullptr;
    if (!pointer) {
        ++rt.stats.failures;
        return nullptr;
    }
    ++rt.stats.allocations;
    ++rt.stats.live;
    rt.stats.live_bytes += bytes;
    if (rt.stats.live_bytes > rt.stats.peak_bytes) rt.stats.peak_bytes = rt.stats.live_bytes;
    return pointer;
}

inline void release(void *pointer, size_t bytes) {
    if (!pointer) return;
    Runtime &rt = runtime();
    if (rt.allocator.release) rt.allocator.release(pointer);
    ++rt.stats.releases;
    if (rt.stats.live) --rt.stats.live;
    rt.stats.live_bytes = rt.stats.live_bytes >= bytes ? rt.stats.live_bytes - bytes : 0;
}

template <class State>
void destroyState(void *memory) {
    static_cast<State *>(memory)->~State();
    release(memory, sizeof(State));
}

} // namespace detail

// Host tests only: replace the allocation pair and reset bookkeeping.
inline void setAllocator(detail::AllocateFn allocate, detail::ReleaseFn release) {
    detail::runtime().allocator.allocate = allocate;
    detail::runtime().allocator.release = release;
}

inline void resetAllocator() {
    detail::runtime().allocator.allocate = detail::platformAllocate;
    detail::runtime().allocator.release = detail::platformRelease;
}

inline const detail::Stats &allocationStats() { return detail::runtime().stats; }

// Test helper: drop retained save buffers and admission bits. Production never
// calls this; it exists so host tests can observe fresh-start behaviour.
inline void releaseRetainedSaves() {
    detail::Runtime &rt = detail::runtime();
    for (uint8_t i = 0; i < kPortIdCount; ++i) {
        if (rt.saves[i].bytes) detail::release(rt.saves[i].bytes, 1024);
        rt.saves[i].bytes = nullptr;
    }
}

// Claim a port id and allocate its typed State with value-initialization. The
// value-initialization keeps the old static-storage guarantee that plain scalar
// globals start at zero; members with explicit initializers keep them.
template <class State>
State *createPortState(uint8_t portId) {
    if (portId >= kPortIdCount) return nullptr;
    detail::Runtime &rt = detail::runtime();
    const uint32_t bit = uint32_t(1) << portId;
    if (rt.admitted & bit) return nullptr; // reject duplicate, keep the old save
    rt.admitted |= bit;
    void *memory = detail::allocate(sizeof(State));
    if (!memory) {
        rt.admitted &= ~bit;
        return nullptr;
    }
    return ::new (memory) State();
}

template <class State>
void destroyPortState(uint8_t portId, State *state) {
    if (!state) return;
    detail::Runtime &rt = detail::runtime();
    state->~State();
    detail::release(state, sizeof(State));
    if (portId < kPortIdCount) rt.admitted &= ~(uint32_t(1) << portId);
}

// Reserve or reuse the retained session buffer for one port. `bytes` is 0 for
// the audited ports that never touch EEPROM; those get no allocation.
// Nonzero declarations currently all request 1,024 bytes. Smaller nonzero values
// also reserve the full logical address space; this is not a compact-save API.
// Contents are copied into the live engine so the logical address space keeps
// initial 0xFF bytes, typed get/put behaviour and out-of-range read=0xFF /
// write=discard semantics of Arduboy2Compat::RamEeprom.
inline bool attachSaveSlot(uint8_t portId, uint16_t bytes, Arduboy2Compat::RamEeprom &eeprom) {
    if (bytes == 0) return true;
    if (portId >= kPortIdCount || bytes > 1024) return false;
    detail::Runtime &rt = detail::runtime();
    detail::SaveSlot &slot = rt.saves[portId];
    if (!slot.bytes) {
        void *memory = detail::allocate(1024);
        if (!memory) return false;
        memset(memory, 0xFF, 1024);
        slot.bytes = static_cast<uint8_t *>(memory);
    }
    memcpy(eeprom.bytes, slot.bytes, 1024);
    return true;
}

// Copy the live engine's RAM EEPROM back to the retained session buffer. Called
// once, from the owning Application destructor, before the State is freed.
inline void commitSaveSlot(uint8_t portId, const Arduboy2Compat::RamEeprom &eeprom) {
    if (portId >= kPortIdCount) return;
    detail::SaveSlot &slot = detail::runtime().saves[portId];
    if (!slot.bytes) return;
    memcpy(slot.bytes, eeprom.bytes, 1024);
}

// Audited zero-save ports count any unexpected EEPROM access so tests can fail
// loudly instead of silently dropping semantics.
inline void guardZeroSave(Arduboy2Compat::RamEeprom &eeprom, uint16_t *counter) {
    eeprom.unexpectedAccess = counter;
}

} // namespace CrabitArduboy

#endif
