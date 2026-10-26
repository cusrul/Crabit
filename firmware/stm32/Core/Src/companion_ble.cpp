#include "companion_ble.h"
#include "companion_trace.h"
#include "../../../shared/companion_session.h"
#include "../../../shared/companion_probe.h"
#include "app_real/app_coproc.h"
#include "app_real/app_cyber_link.h"
#include "app_real/components/companion_pair_prompt.h"
#include "ameba_session.h"
#include "sys_power.h"
#include <atomic>

namespace Crabit { namespace CompanionBle {
namespace {
CompanionSession state;
std::atomic<bool> hardware_active{false}, hardware_transitioning{false};
uint16_t next_session = 0;
CompanionPairPrompt pair_prompt(confirm, cancel);
bool legacy_active = false, legacy_stop_requested = false;
bool resume_after_ui = false;
bool probe_pong = false;
enum class ModePhase { Off, Starting, Running, StopRequested, Stopping, Failed };
ModePhase mode_phase = ModePhase::Off;
const char* mode_status = "OFF";
uint32_t mode_off_at = 0;
bool mode_advertising_seen = false, mode_connected = false, mode_start_failed = false;
uint16_t mode_probe_id = 0;
uint32_t mode_probe_at = 0;
bool mode_probe_sent = false, mode_probe_pending = false;
bool mode_uart_verified = false;
const char* mode_pair_status = "DISABLED";
struct ModePairState {
    uint32_t sequence = 0, last_sequence = 0, received_at = 0, reply_at = 0;
    bool awaiting_reply = false, authenticated = false;
} mode_pair;

bool modeActive() {
    return mode_phase != ModePhase::Off && mode_phase != ModePhase::Failed;
}
bool modeTransitioning() {
    return modeActive() && mode_phase != ModePhase::Running;
}

void synchronizeFlags() {
    hardware_active.store(state.active() || legacy_active || modeActive(), std::memory_order_release);
    hardware_transitioning.store(state.transitioning() || legacy_stop_requested || modeTransitioning(), std::memory_order_release);
}
void dismissPairPopup() {
    pair_prompt.dismiss();
}

void resetModePair(bool new_session = false) {
    const uint32_t previous = mode_pair.last_sequence;
    mode_pair = ModePairState{};
    if (!new_session) mode_pair.last_sequence = previous;
    dismissPairPopup();
}

bool parsePairSequence(const char* value, const char* prefix, uint32_t& sequence) {
    const size_t length = strlen(prefix);
    if (strncmp(value, prefix, length) || value[length] != ':') return false;
    const char* digit = value + length + 1;
    if (!*digit) return false;
    uint32_t result = 0;
    for (; *digit; ++digit) {
        if (*digit < '0' || *digit > '9' ||
                result > (UINT32_MAX - uint32_t(*digit - '0')) / 10) return false;
        result = result * 10 + uint32_t(*digit - '0');
    }
    if (!result) return false;
    sequence = result;
    return true;
}

void receiveModePair(const char* value) {
    uint32_t sequence = 0;
    if (parsePairSequence(value, "PAIR_WAIT", sequence)) {
        if (mode_pair.last_sequence && int32_t(sequence - mode_pair.last_sequence) <= 0) return;
        resetModePair();
        mode_pair.sequence = mode_pair.last_sequence = sequence;
        mode_pair.received_at = HAL_GetTick();
        mode_pair_status = "PAIR WAIT";
    } else if (parsePairSequence(value, "AUTHENTICATED", sequence)) {
        const bool confirmed = sequence == mode_pair.sequence && mode_pair.awaiting_reply;
        // A new report may also mean a previously confirmed RAM token reconnected.
        const bool cached = !mode_pair.last_sequence || int32_t(sequence - mode_pair.last_sequence) > 0;
        if (!mode_uart_verified || !mode_connected || (!confirmed && !cached)) return;
        resetModePair();
        mode_pair.last_sequence = sequence;
        mode_pair.authenticated = true;
        mode_pair_status = "RAM AUTHENTICATED";
    } else {
        const char* result = nullptr;
        if (parsePairSequence(value, "PAIR_TIMEOUT", sequence)) result = "PAIR TIMEOUT";
        else if (parsePairSequence(value, "PAIR_CANCELLED", sequence)) result = "PAIR DENIED";
        else if (parsePairSequence(value, "PAIR_REJECTED", sequence)) result = "PAIR REJECTED";
        if (result && sequence == mode_pair.sequence) {
            resetModePair();
            mode_pair_status = result;
        }
    }
}

bool pairCommand(const char* action, uint16_t session, uint32_t pair) {
    if (modeActive()) {
        if (mode_phase != ModePhase::Running || !mode_uart_verified || !mode_connected ||
                session != mode_probe_id || !pair || pair != mode_pair.sequence ||
                mode_pair.awaiting_reply || uint32_t(HAL_GetTick() - mode_pair.received_at) >= 30000)
            return false;
    } else if (!state.canConfirm(session, pair)) return false;
    char value[32];
    snprintf(value, sizeof(value), "%s:%lu", action, static_cast<unsigned long>(pair));
    if (!EventBus::tryPublish(modeActive() ? "sys/bw/pair" : "sys/comm", value, session)) {
        Popup::showToast("PAIR SEND FAILED");
        return false;
    }
    if (modeActive()) {
        mode_pair.awaiting_reply = true;
        mode_pair.reply_at = HAL_GetTick();
        mode_pair_status = strcmp(action, "CONFIRM") == 0 ? "CONFIRM WAIT" : "DENY WAIT";
    }
    dismissPairPopup();
    return true;
}
}

bool active() { return state.active() || legacy_active || modeActive(); }
bool transitioning() { return state.transitioning() || legacy_stop_requested || modeTransitioning(); }
bool hardwareActive() { return hardware_active.load(std::memory_order_acquire); }
bool hardwareTransitioning() { return hardware_transitioning.load(std::memory_order_acquire); }
bool uartProbeOnly() { return CompanionProbe::Enabled; }
bool legacyAutostartAllowed() { return !CompanionProbe::SkipLegacyAutostart; }
void observeProbePong(const char* value) {
    if (CompanionProbe::Enabled && state.phase == CompanionSession::Phase::Starting &&
            CompanionProbe::isPong(value)) {
        probe_pong = true;
        CompanionTrace::record(CompanionTrace::ProbePongAccepted, state.session);
    }
}
const char* status() {
    if (!CompanionProbe::Enabled && mode_phase != ModePhase::Off) return mode_status;
    if (CompanionProbe::Enabled) {
        // Legacy PONG has no request id. Report observation, not authentication
        // or a correlated round-trip PASS (old receive data remains a suspect).
        if (state.phase == CompanionSession::Phase::Starting) return probe_pong ? "PONG SEEN" : "WAIT PONG";
        if (strcmp(state.status, "START TIMEOUT") == 0) return probe_pong ? "PONG SEEN / OFF" : "PING TIMEOUT";
        if (strcmp(state.status, "STOP TIMEOUT") == 0) return "PING STOPPED";
    }
    return legacy_active ? "LEGACY BLE ON" : state.status;
}
uint16_t sessionId() { return modeActive() ? mode_probe_id : state.session; }
uint32_t pairSequence() { return modeActive() ? mode_pair.sequence : state.pair; }
const char* pairTransportStatus() { return mode_pair_status; }
bool start() {
    if (amebaSessionActive() || g_coproc_active || AppManager::frameAppActive() ||
            modeActive() || state.active()) return false;
    // Do not power-cycle an untracked legacy autostart session. The new route
    // is opt-in; the old persisted setting remains readable and unchanged.
    if (legacy_active) { Popup::showToast("STOP LEGACY BLE FIRST"); return false; }
    if (!CompanionProbe::Enabled) {
        // Startup remains a FRAME app until the ordinary mode reports ready.
        // Do not move its proven boot/timing into the old background START path.
        mode_phase = ModePhase::Starting;
        mode_status = "STARTING";
        mode_advertising_seen = mode_connected = mode_start_failed = false;
        if (++next_session == 0) ++next_session;
        mode_probe_id = next_session;
        mode_probe_sent = mode_probe_pending = false;
        mode_uart_verified = false;
        resetModePair(true);
        mode_pair_status = "DISABLED";
        synchronizeFlags();
        SysPower::feed();
        AppManager::launch(new CyberLinkApp(CyberLinkApp::Mode::BleBackground));
        return true;
    }
    if (++next_session == 0) ++next_session;
    if (!state.start(next_session)) return false;
    probe_pong = false;
    CompanionTrace::record(CompanionTrace::Start, state.session);
    if (CompanionProbe::Enabled) CompanionTrace::record(CompanionTrace::ProbeOnly, state.session);
    else if (CompanionProbe::SkipLegacyAutostart) CompanionTrace::record(CompanionTrace::ManualBleOnly, state.session);
    synchronizeFlags();
    SysPower::feed();
    return true;
}
bool stop() {
    if (modeActive()) {
        // The FRAME app owns startup cancellation through its physical escape.
        if (mode_phase != ModePhase::Running) return false;
        CompanionTrace::record(CompanionTrace::BleStopRequested, mode_probe_id);
        mode_phase = ModePhase::StopRequested;
        mode_status = "STOPPING";
        mode_probe_id = 0;
        mode_probe_pending = false;
        mode_uart_verified = false;
        resetModePair();
        mode_pair_status = "DISABLED";
        synchronizeFlags();
        return true;
    }
    if (legacy_active) {
        CompanionTrace::record(CompanionTrace::LegacyStop);
        legacy_stop_requested = true;
        synchronizeFlags();
        return true;
    }
    if (!state.stop()) return false;
    dismissPairPopup();
    synchronizeFlags();
    return true;
}
bool toggle() { return active() ? stop() : start(); }
void resumeAfterUi() {
    resume_after_ui = true;
    CompanionTrace::record(CompanionTrace::BleResumeQueued);
}
bool confirm(uint16_t id, uint32_t pair) { return pairCommand("CONFIRM", id, pair); }
bool cancel(uint16_t id, uint32_t pair) { return pairCommand("CANCEL", id, pair); }
bool adoptModeBle() {
    if (mode_phase != ModePhase::Starting || !mode_advertising_seen || mode_start_failed) return false;
    mode_phase = ModePhase::Running;
    mode_status = mode_connected ? "CONNECTED" : "ADVERTISING";
    synchronizeFlags();
    return true;
}
void finishModeBleStart(const char* result) {
    if (mode_phase != ModePhase::Starting) return;
    mode_status = result ? result : "BLE START FAILED";
    mode_phase = strcmp(mode_status, "OFF") == 0 ? ModePhase::Off : ModePhase::Failed;
    synchronizeFlags();
    SysPower::feed();
}
void observeModeStatus(const char* value) {
    if (!value || (mode_phase != ModePhase::Starting && mode_phase != ModePhase::Running)) return;
    // Latch events, not the final GlobalStore text: one drain may contain both
    // ADVERTISING and the legacy connection status READY. READY alone cannot
    // establish startup, and it is transport connection, never authentication.
    if (!strcmp(value, "ADVERTISING")) mode_advertising_seen = true;
    if (!strcmp(value, "READY") || !strcmp(value, "CONNECTED")) mode_connected = true;
    else if (!strcmp(value, "DISCONNECTED") || !strcmp(value, "ADVERTISING")) mode_connected = false;
    if (!strcmp(value, "DISCONNECTED")) {
        resetModePair();
        mode_pair_status = mode_uart_verified ? "UART PASS / RAM" : "DISABLED";
    }
    if (!strcmp(value, "FAILED")) {
        mode_start_failed = true;
        if (mode_phase == ModePhase::Running) {
            mode_status = "BLE FAILED";
            mode_phase = ModePhase::StopRequested;
            mode_probe_id = 0;
            mode_probe_pending = false;
            mode_uart_verified = false;
            resetModePair();
            mode_pair_status = "DISABLED";
            synchronizeFlags();
        }
    } else if (mode_phase == ModePhase::Running) {
        mode_status = mode_connected ? "CONNECTED" : "ADVERTISING";
    }
}
void receive(uint16_t id, const char* value) {
    // This experiment must never turn a stale radio status into READY/auth.
    if (CompanionProbe::Enabled) return;
    if (modeActive()) {
        if (!value || mode_phase != ModePhase::Running || id != mode_probe_id) return;
        if (!strcmp(value, "CONNECTED")) { mode_connected = true; return; }
        if (!strcmp(value, "DISCONNECTED")) {
            mode_connected = false;
            resetModePair();
            mode_pair_status = mode_uart_verified ? "UART PASS / RAM" : "DISABLED";
            return;
        }
        if (strncmp(value, "UART_", 5) != 0) {
            receiveModePair(value);
            return;
        }
        if (!mode_probe_pending) return;
        if (uint32_t(HAL_GetTick() - mode_probe_at) >= 2000) {
            mode_probe_pending = false;
            mode_pair_status = "UART TIMEOUT";
            CompanionTrace::record(CompanionTrace::UartProbeTimeout, mode_probe_id);
        } else if (!strcmp(value, "UART_OK")) {
            mode_probe_pending = false;
            mode_uart_verified = true;
            mode_pair_status = "UART PASS / RAM";
            CompanionTrace::record(CompanionTrace::UartProbePass, mode_probe_id);
        } else if (!strcmp(value, "UART_NOT_READY")) {
            mode_probe_pending = false;
            mode_pair_status = "UART NOT READY";
            CompanionTrace::record(CompanionTrace::UartProbeFailed, mode_probe_id);
        }
        return;
    }
    const auto before = state.phase;
    state.receive(id, value, HAL_GetTick());
    CompanionTrace::record(CompanionTrace::Phase, id, uint32_t(before), uint32_t(state.phase));
    pair_prompt.invalidate(state.session, state.pair);
    synchronizeFlags();
}
void registerLegacyAutostart() {
    // Track only the known legacy boot branch. Never infer ownership just from
    // a persisted bit, adopt authentication, or reboot an existing connection.
    legacy_active = true;
    synchronizeFlags();
}
void tick() {
    // onUnmount runs before AppManager pops its FRAME owner. Re-enter only on
    // a later UI tick, after both the app and physical OLED ownership are gone.
    if (resume_after_ui && !amebaSessionActive() && !g_coproc_active &&
            !AppManager::frameAppActive()) {
        resume_after_ui = false;
        if (!active() && !start()) Popup::showToast("蓝牙恢复失败");
    }
    if (modeActive()) {
        switch (mode_phase) {
        case ModePhase::Starting: break; // All boot work belongs to FRAME.
        case ModePhase::Running:
            // One read-only post-BLE UART roundtrip. No ATTACH, session adoption,
            // pairing confirmation, automatic retry or radio restart on failure.
            if (!mode_probe_sent) {
                mode_probe_sent = true;
                mode_probe_at = HAL_GetTick();
                mode_probe_pending = EventBus::tryPublish("sys/bw/ping", "BLE_UART", mode_probe_id);
                mode_pair_status = mode_probe_pending ? "UART WAIT" : "UART SEND FAIL";
                CompanionTrace::record(mode_probe_pending ? CompanionTrace::UartProbeQueued :
                    CompanionTrace::UartProbeFailed, mode_probe_id);
            } else if (mode_probe_pending && uint32_t(HAL_GetTick() - mode_probe_at) >= 2000) {
                mode_probe_pending = false;
                mode_pair_status = "UART TIMEOUT";
                CompanionTrace::record(CompanionTrace::UartProbeTimeout, mode_probe_id);
            }
            if (mode_pair.sequence &&
                    (mode_pair.awaiting_reply ? uint32_t(HAL_GetTick() - mode_pair.reply_at) >= 2000 :
                                               uint32_t(HAL_GetTick() - mode_pair.received_at) >= 30000)) {
                const bool waiting = mode_pair.awaiting_reply;
                resetModePair();
                mode_pair_status = waiting ? "PAIR REPLY TIMEOUT" : "PAIR TIMEOUT";
            }
            break;
        case ModePhase::StopRequested:
            BW16_ForceOff();
            mode_off_at = HAL_GetTick();
            mode_phase = ModePhase::Stopping;
            break;
        case ModePhase::Stopping:
            if (uint32_t(HAL_GetTick() - mode_off_at) >= 250) {
                mode_phase = strcmp(mode_status, "BLE FAILED") == 0 ? ModePhase::Failed : ModePhase::Off;
                if (mode_phase == ModePhase::Off) mode_status = "OFF";
                CompanionTrace::record(CompanionTrace::BleOffComplete, 0,
                    mode_phase == ModePhase::Off ? 1u : 0u);
                GlobalStore.ble_status[0] = '\0';
                SysPower::feed();
            }
            break;
        default: break;
        }
        synchronizeFlags();
        pair_prompt.tick(mode_phase == ModePhase::Running && mode_uart_verified && mode_connected ? mode_probe_id : 0,
                         mode_pair.awaiting_reply ? 0 : mode_pair.sequence);
        return;
    }
    if (legacy_stop_requested) {
        // Explicit user STOP, not implicit takeover. The legacy branch never
        // transferred OLED ownership. Keep its persisted preference unchanged.
        BW16_ForceOff();
        legacy_stop_requested = legacy_active = false;
        synchronizeFlags();
        SysPower::feed();
        return;
    }
    const auto action = state.tick(HAL_GetTick());
    synchronizeFlags(); // Sensor task must see boot ownership before HAL calls.
    switch (action) {
    case CompanionSession::Action::Boot:
        // Reuse the exact proven reset/power/UART sequence; never hand over OLED.
        CompanionTrace::record(CompanionTrace::BootBegin, state.session);
        BW16_NormalBoot();
        CompanionTrace::record(CompanionTrace::BootEnd, state.session);
        state.booted(HAL_GetTick());
        break;
    case CompanionSession::Action::PrepareAndStart: {
        const bool ready = STM32_PrepareAmebaUartRx();
        CompanionTrace::record(CompanionTrace::RxPrepared, state.session, ready);
        const bool sent = ready && EventBus::tryPublish(CompanionProbe::topic(), CompanionProbe::value(), state.session);
        CompanionTrace::record(CompanionTrace::StartQueued, state.session, ready, sent);
        state.started(sent, HAL_GetTick());
        break;
    }
    case CompanionSession::Action::Stop:
        // Probe never acquired a radio owner. Do not send a radio STOP or fake
        // its acknowledgement; the existing 2s stop deadline releases power.
        if (!CompanionProbe::Enabled) state.stopSent(EventBus::tryPublish("sys/comm", "STOP", state.session));
        break;
    case CompanionSession::Action::PowerOff:
        CompanionTrace::record(CompanionTrace::PowerOff, state.session, uint32_t(state.phase));
        BW16_ForceOff();
        state.poweredOff();
        SysPower::feed();
        dismissPairPopup();
        break;
    default: break;
    }
    static auto last_phase = CompanionSession::Phase::Off;
    if (last_phase != state.phase) {
        CompanionTrace::record(CompanionTrace::Phase, state.session, uint32_t(last_phase), uint32_t(state.phase));
        last_phase = state.phase;
    }
    synchronizeFlags();
    pair_prompt.tick(state.session, state.pair);
}
} }
