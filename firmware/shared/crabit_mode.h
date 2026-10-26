#ifndef CRABIT_MODE_H
#define CRABIT_MODE_H

#include <stdint.h>

// Pure coordination state shared by STM32 and BW16.  It carries intent and
// liveness only; platform code remains responsible for the protected power
// and boot sequences.
namespace CrabitMode {

// CrabitWire topics reserved for coordination. Values are compact CSV or a
// platform-defined status payload; they must not trigger hardware directly.
static const char* const TOPIC_REQUEST = "mode/request";
static const char* const TOPIC_GRANT = "mode/grant";
static const char* const TOPIC_ACTIVE = "mode/active";
static const char* const TOPIC_HEARTBEAT = "mode/heartbeat";
static const char* const TOPIC_RELEASE = "mode/release";
static const char* const TOPIC_STATUS = "mode/status";

enum class Mode : uint8_t {
    WATCH_IDLE = 0,
    WIRELESS_SERVICE = 1,
    BW16_EXPERIENCE = 2,
    OTA_UPDATE = 3,
    FAULT_RECOVERY = 4
};

enum class Phase : uint8_t {
    IDLE = 0,
    PREPARING = 1,
    GRANTED = 2,
    ACTIVE = 3,
    RELEASING = 4,
    DENIED = 5,
    FAULT = 6
};

enum class Owner : uint8_t { NONE = 0, STM32 = 1, BW16 = 2 };

enum Resource : uint32_t {
    RESOURCE_NONE = 0,
    RESOURCE_DISPLAY = 1u << 0,
    RESOURCE_INPUT = 1u << 1,
    RESOURCE_UART_BANDWIDTH = 1u << 2,
    RESOURCE_HIGH_PERFORMANCE = 1u << 3
};

struct Lease {
    Mode mode;
    Phase phase;
    Owner owner;
    uint32_t resources;
    uint32_t epoch;
    uint32_t requested_at_ms;
    uint32_t deadline_ms;
    uint32_t heartbeat_at_ms;
};

inline const char* mode_name(Mode mode) {
    switch (mode) {
        case Mode::WATCH_IDLE: return "WATCH_IDLE";
        case Mode::WIRELESS_SERVICE: return "WIRELESS_SERVICE";
        case Mode::BW16_EXPERIENCE: return "BW16_EXPERIENCE";
        case Mode::OTA_UPDATE: return "OTA_UPDATE";
        case Mode::FAULT_RECOVERY: return "FAULT_RECOVERY";
    }
    return "UNKNOWN";
}

// Bounded, allocation-free state machine.  The coordinator never toggles a
// GPIO or power rail; callers observe the lease and perform platform actions.
class Coordinator {
public:
    Coordinator() : lease_{} {
        lease_.mode = Mode::WATCH_IDLE;
        lease_.phase = Phase::IDLE;
        lease_.owner = Owner::NONE;
    }

    bool request(Mode mode, Owner owner, uint32_t resources,
                 uint32_t now_ms, uint32_t timeout_ms) {
        if (lease_.phase != Phase::IDLE && lease_.phase != Phase::DENIED) return false;
        lease_.mode = mode;
        lease_.phase = Phase::PREPARING;
        lease_.owner = owner;
        lease_.resources = resources;
        lease_.epoch++;
        lease_.requested_at_ms = now_ms;
        lease_.deadline_ms = now_ms + timeout_ms;
        lease_.heartbeat_at_ms = now_ms;
        return true;
    }

    bool grant(uint32_t now_ms) {
        if (lease_.phase != Phase::PREPARING || expired(now_ms)) return false;
        lease_.phase = Phase::GRANTED;
        lease_.heartbeat_at_ms = now_ms;
        return true;
    }

    bool activate(uint32_t now_ms) {
        if (lease_.phase != Phase::GRANTED || expired(now_ms)) return false;
        lease_.phase = Phase::ACTIVE;
        lease_.heartbeat_at_ms = now_ms;
        return true;
    }

    bool release(uint32_t now_ms) {
        if (lease_.phase != Phase::ACTIVE && lease_.phase != Phase::GRANTED) return false;
        lease_.phase = Phase::RELEASING;
        lease_.heartbeat_at_ms = now_ms;
        return true;
    }

    void released() {
        lease_.phase = Phase::IDLE;
        lease_.mode = Mode::WATCH_IDLE;
        lease_.owner = Owner::NONE;
        lease_.resources = RESOURCE_NONE;
    }

    void deny() { lease_.phase = Phase::DENIED; }

    bool heartbeat(uint32_t now_ms) {
        if (lease_.phase != Phase::ACTIVE && lease_.phase != Phase::GRANTED) return false;
        lease_.heartbeat_at_ms = now_ms;
        return true;
    }

    bool tick(uint32_t now_ms, uint32_t heartbeat_timeout_ms) {
        if (lease_.phase == Phase::PREPARING || lease_.phase == Phase::GRANTED) {
            if (expired(now_ms)) { lease_.phase = Phase::DENIED; return true; }
        } else if (lease_.phase == Phase::ACTIVE &&
                   (uint32_t)(now_ms - lease_.heartbeat_at_ms) >= heartbeat_timeout_ms) {
            lease_.phase = Phase::FAULT;
            return true;
        }
        return false;
    }

    const Lease& lease() const { return lease_; }

private:
    bool expired(uint32_t now_ms) const {
        return (int32_t)(now_ms - lease_.deadline_ms) >= 0;
    }
    Lease lease_;
};

} // namespace CrabitMode

#endif
