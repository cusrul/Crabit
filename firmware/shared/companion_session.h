#ifndef CRABIT_COMPANION_SESSION_H
#define CRABIT_COMPANION_SESSION_H

#include <stdint.h>
#include <string.h>

namespace Crabit {
// Product session policy only. No SDK, display, task or allocation dependency.
class CompanionSession {
public:
    enum class Phase { Off, BootRequested, Booting, BootWait, Preparing,
        Starting, Advertising, Connected, Authenticated, StopRequested,
        Stopping, PowerOffRequested, PoweringOff, Failed };
    enum class Action { None, Boot, PrepareAndStart, Stop, PowerOff };
    Phase phase = Phase::Off;
    uint16_t session = 0;
    uint32_t pair = 0;
    const char* status = "OFF";

    bool active() const { return phase != Phase::Off && phase != Phase::Failed; }
    bool transitioning() const {
        return active() && phase != Phase::Advertising &&
            phase != Phase::Connected && phase != Phase::Authenticated;
    }
    bool start(uint16_t id) {
        if (active() || !id) return false;
        session = id; pair = 0; failed = false;
        phase = Phase::BootRequested; status = "BOOTING";
        return true;
    }
    bool stop() {
        if (!active() || phase == Phase::Stopping ||
            phase == Phase::PowerOffRequested || phase == Phase::PoweringOff) return false;
        if (phase == Phase::BootRequested) {
            phase = Phase::Off; pair = 0; status = "OFF"; return true;
        }
        pair = 0; phase = Phase::StopRequested; status = "STOPPING";
        return true;
    }
    Action tick(uint32_t now) {
        // Lost UART status must not leave a physical confirmation prompt alive
        // forever. RadioSvc owns the actual 30 s authorization deadline.
        if (pair && uint32_t(now - pair_since) >= 31000) {
            pair = 0; status = "PAIR EXPIRED";
        }
        switch (phase) {
        case Phase::BootRequested:
            phase = Phase::Booting; return Action::Boot;
        case Phase::BootWait:
            if (uint32_t(now - since) >= 1200) {
                phase = Phase::Preparing; return Action::PrepareAndStart;
            }
            break;
        case Phase::Starting:
            if (uint32_t(now - since) >= 7000) fail("START TIMEOUT");
            break;
        case Phase::StopRequested:
            phase = Phase::Stopping; since = now; return Action::Stop;
        case Phase::Stopping:
            if (uint32_t(now - since) >= 2000) {
                failed = true; status = "STOP TIMEOUT"; phase = Phase::PowerOffRequested;
            }
            break;
        default: break;
        }
        if (phase == Phase::PowerOffRequested) {
            phase = Phase::PoweringOff; return Action::PowerOff;
        }
        return Action::None;
    }
    void booted(uint32_t now) {
        if (phase != Phase::Booting) return;
        phase = Phase::BootWait; since = now; status = "WAIT UART";
    }
    void started(bool sent, uint32_t now) {
        if (phase != Phase::Preparing) return;
        if (!sent) { fail("UART/QUEUE FAIL"); return; }
        phase = Phase::Starting; since = now; status = "WAIT BLE";
    }
    void stopSent(bool sent) { if (!sent) fail("STOP QUEUE FAIL"); }
    void poweredOff() {
        if (phase != Phase::PoweringOff) return;
        pair = 0; phase = failed ? Phase::Failed : Phase::Off;
        if (!failed) status = "OFF";
    }
    bool canConfirm(uint16_t id, uint32_t sequence) const {
        return id == session && sequence && sequence == pair &&
            phase == Phase::Connected;
    }
    void receive(uint16_t id, const char* value, uint32_t now = 0) {
        if (!value || id != session || !active()) return;
        if (phase == Phase::Stopping) {
            if (strcmp(value, "STOPPED") == 0) phase = Phase::PowerOffRequested;
            return;
        }
        if (phase != Phase::Starting && phase != Phase::Advertising &&
            phase != Phase::Connected && phase != Phase::Authenticated) return;
        if (strcmp(value, "READY") == 0) {
            if (phase == Phase::Starting) { phase = Phase::Advertising; status = "BLE READY"; }
        } else if (strcmp(value, "CONNECTED") == 0) {
            phase = Phase::Connected; pair = 0; status = "CONNECTED";
        } else if (strcmp(value, "AUTHENTICATED") == 0) {
            phase = Phase::Authenticated; pair = 0; status = "AUTHENTICATED";
        } else if (strcmp(value, "DISCONNECTED") == 0) {
            phase = Phase::Advertising; pair = 0; status = "BLE READY";
        } else if (strncmp(value, "PAIR_WAIT:", 10) == 0 &&
                   (phase == Phase::Connected || phase == Phase::Authenticated)) {
            uint32_t sequence = 0;
            const char* digit = value + 10;
            if (!*digit) return;
            for (; *digit; ++digit) {
                if (*digit < '0' || *digit > '9' ||
                    sequence > (UINT32_MAX - uint32_t(*digit - '0')) / 10) return;
                sequence = sequence * 10 + uint32_t(*digit - '0');
            }
            if (sequence) {
                phase = Phase::Connected; pair = sequence; pair_since = now; status = "PAIR WAIT";
            }
        } else if (strcmp(value, "PAIR_TIMEOUT") == 0 ||
                   strcmp(value, "PAIR_CANCELLED") == 0) {
            pair = 0; status = "PAIR NOT APPROVED";
        } else if (strcmp(value, "FAILED") == 0 || strcmp(value, "BUSY") == 0 ||
                   strcmp(value, "REJECTED") == 0) fail("BLE START FAILED");
    }
private:
    uint32_t since = 0;
    uint32_t pair_since = 0;
    bool failed = false;
    void fail(const char* reason) {
        failed = true; pair = 0; status = reason; phase = Phase::PowerOffRequested;
    }
};

// Clock-only physical shortcut; re-arm only after both keys are released.
class CompanionShortcut {
    uint32_t pressed_at = 0;
    bool holding = false, latched = false;
public:
    bool update(bool up, bool down, uint32_t now) {
        if (!up && !down) { holding = latched = false; return false; }
        if (latched) return false;
        if (!up || !down) { holding = false; return false; }
        if (!holding) { holding = true; pressed_at = now; return false; }
        if (uint32_t(now - pressed_at) < 1000) return false;
        latched = true; return true;
    }
};
} // namespace Crabit
#endif
