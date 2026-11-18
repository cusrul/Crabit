#ifndef CRABIT_BW16_UI_LINK_H
#define CRABIT_BW16_UI_LINK_H

#include <stdint.h>
#include <string.h>

// Correlated BW16 UI handshake state. This is deliberately separate from the
// generic EventBus ACK path: only sys/ack values that echo the request id of
// the current UI command can change this state. Uncorrelated legacy traffic
// (request_id == 0) is ignored and keeps the old best-effort behaviour.
//
// The transport (AppServices) installs the sys/ack observer; the display app
// only reads and starts a request. Inline function-local storage keeps the
// header usable from the STM32 build and the host fixture without a new
// translation unit.
namespace Crabit {
namespace Bw16Ui {

enum class Status : uint8_t { Idle, Waiting, Ready, Failed };

inline Status& statusStorage() {
    static Status value = Status::Idle;
    return value;
}
inline uint16_t& requestStorage() {
    static uint16_t value = 0;
    return value;
}
inline uint16_t& sequenceStorage() {
    static uint16_t value = 0;
    return value;
}
inline const char*& failureStorage() {
    static const char* value = nullptr;
    return value;
}

inline Status status() { return statusStorage(); }
inline uint16_t requestId() { return requestStorage(); }
inline const char* failure() { return failureStorage(); }

// Allocates a non-zero id, skipping zero on wrap. The id is echoed by the BW16
// in sys/ack together with UI_READY/UI_FAILED/REJECTED/BUSY. UI ids use the
// upper half of the space so a late BLE session id cannot collide with them.
inline uint16_t begin() {
    statusStorage() = Status::Waiting;
    failureStorage() = nullptr;
    uint16_t next = sequenceStorage();
    if (next < 0x8000u || next == 0xFFFFu) next = 0x8000u;
    else ++next;
    sequenceStorage() = next;
    requestStorage() = next;
    return next;
}

inline void cancel() {
    statusStorage() = Status::Idle;
    requestStorage() = 0;
    failureStorage() = nullptr;
}

inline bool matches(uint16_t id) {
    return id != 0 && id == requestStorage();
}

// Returns true only when this ack belongs to the active UI request. A stale or
// legacy acknowledgement (including the request_id == 0 path used by older
// STM32 firmware) must not be treated as a successful UI claim.
inline bool acceptAck(uint16_t id, const char* value) {
    if (!matches(id)) return false;
    if (statusStorage() != Status::Waiting) return true;  // duplicate ack
    if (value == nullptr) return false;
    if (strcmp(value, "UI_READY") == 0) {
        statusStorage() = Status::Ready;
    } else if (strcmp(value, "UI_FAILED") == 0) {
        statusStorage() = Status::Failed;
        failureStorage() = "UI NOT READY";
    } else if (strcmp(value, "REJECTED") == 0) {
        statusStorage() = Status::Failed;
        failureStorage() = "UI REJECTED";
    } else if (strcmp(value, "BUSY") == 0) {
        statusStorage() = Status::Failed;
        failureStorage() = "UI BUSY";
    } else {
        // Same id but not a UI outcome: leave it for the generic path.
        return false;
    }
    return true;
}

}  // namespace Bw16Ui
}  // namespace Crabit

#endif  // CRABIT_BW16_UI_LINK_H
