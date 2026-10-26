#ifndef CRABIT_RADIO_SVC_H
#define CRABIT_RADIO_SVC_H

#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace RadioSvc {

inline bool pairDeadlineExpired(uint32_t now, uint32_t deadline) {
    return (int32_t)(now - deadline) >= 0;
}

inline bool parseCompanionValue(const char* value, char* action, size_t capacity,
                                uint32_t* pair_seq) {
    if (!value || !action || capacity == 0 || !pair_seq) return false;
    *pair_seq = 0;
    const char* colon = strchr(value, ':');
    const size_t length = colon ? size_t(colon - value) : strlen(value);
    if (!length || length >= capacity) return false;
    memcpy(action, value, length); action[length] = '\0';
    const bool pairing = strcmp(action, "CONFIRM") == 0 || strcmp(action, "CANCEL") == 0;
    if (!colon) return true;
    if (!pairing) return false;
    if (colon[1] == '\0') return true;
    uint32_t parsed = 0;
    for (const char* digit = colon + 1; *digit; ++digit) {
        if (*digit < '0' || *digit > '9') return true;
        const uint32_t part = uint32_t(*digit - '0');
        if (parsed > (UINT32_MAX - part) / 10) return true;
        parsed = parsed * 10 + part;
    }
    *pair_seq = parsed;
    return true;
}

enum State : uint8_t {
    IDLE = 0,
    STARTING,
    ADVERTISING,
    CONNECTED,
    STOPPING,
    FAILED,
    TIMEOUT
};

enum Result : uint8_t {
    QUEUED = 0,
    ACQUIRED,
    BUSY,
    REJECTED,
    FAILED_RESULT,
    STOPPED_RESULT,
    TIMEOUT_RESULT
};

enum PairDiagnosticState : uint8_t {
    PAIR_DIAG_OFF = 0,
    PAIR_DIAG_IDLE,
    PAIR_DIAG_WAITING,
    PAIR_DIAG_RAM_CONFIRMED,
    PAIR_DIAG_CANCELLED,
    PAIR_DIAG_TIMEOUT
};

struct Snapshot {
    State state;
    Result last_result;
    uint32_t generation;
    uint32_t state_since_ms;
    uint16_t request_id;
    uint8_t conn_id;
    bool authenticated;
    bool initialized;
    PairDiagnosticState pair_diagnostic_state;
    uint16_t pair_rx_length;
    bool pair_diagnostic_active;
    bool ram_pairing_only; // Boot policy, not confirmation or authentication.
};

// setup() records the Arduino task. service() in loop() is the BLE SDK owner;
// UART/BLE callbacks submit bounded queue entries and never run SDK startup.
void init();
void initTracing();
Result requestStart(uint16_t request_id = 0);
Result requestStop(uint16_t request_id = 0);
Result beginPairDiagnostic();
Result confirmPairInRam();
Result cancelPairDiagnostic();
Result endPairDiagnostic();
// Arduino owner only, after ordinary MODE_BLE startup. No companion/ATTACH queue.
Result confirmModePair(uint16_t session_id, const char* action, uint32_t pair_seq);
Result enqueueNotify(const char* payload, uint8_t conn_id);
// Headless companion control. session_id is the nonzero UART request_id.
Result enqueueCompanion(uint16_t session_id, const char* action, uint32_t pair_seq = 0);
// Best-effort fixed-stage trace for a decoded companion command; never includes its payload.
void traceCompanionReceived(uint16_t request_id);
bool companionActive();
bool companionControlsMode();
Snapshot snapshot();
void service();
void tick();

}  // namespace RadioSvc

#endif
