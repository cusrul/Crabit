#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "companion_session.h"
#include "companion_probe.h"
#include "topics.h"

struct TestGlobalStore { char ble_status[16]{}; };
TestGlobalStore GlobalStore;
static uint32_t now_ms;
static unsigned force_off_count, feed_count, launch_count, toast_count, publish_count;
static unsigned boot_count, frame_query_count;
static bool publish_result = true;
static bool frame_active, frame_active_on_second_query;
static char last_toast[64];
static char published_topic[24], published_value[24];
static uint16_t published_id;

// The real CompanionPairPrompt/AppManager/Popup types are deliberately absent:
// this harness only provides the injected ports, so any direct App/UI reference
// left in companion_ble.cpp fails to compile here.
static bool prompt_visible;
static uint16_t prompt_session;
static uint32_t prompt_sequence;
static unsigned dismiss_count, invalidate_count, tick_count, prompt_show_count;
static char hook_log[64];
static unsigned hook_log_len;

static void logHook(char c) {
    if (hook_log_len + 1 < sizeof(hook_log)) {
        hook_log[hook_log_len++] = c;
        hook_log[hook_log_len] = '\0';
    }
}
static void resetHookLog() { hook_log_len = 0; hook_log[0] = '\0'; }

static bool fakeFrameAppActive() {
    logHook('F');
    ++frame_query_count;
    return frame_active || (frame_active_on_second_query && frame_query_count == 2);
}
static void fakeLaunchBleBackground() { logHook('L'); ++launch_count; }
static void fakeShowToast(const char* message) {
    logHook('T');
    ++toast_count;
    snprintf(last_toast, sizeof(last_toast), "%s", message ? message : "");
}
static void fakeDismissPairPrompt() {
    if (!prompt_visible) return;
    logHook('D');
    ++dismiss_count;
    prompt_visible = false;
    prompt_session = 0;
    prompt_sequence = 0;
}
static void fakeInvalidatePairPrompt(uint16_t session, uint32_t sequence) {
    logHook('I');
    ++invalidate_count;
    if (!session || !sequence || session != prompt_session || sequence != prompt_sequence)
        fakeDismissPairPrompt();
}
static void fakeTickPairPrompt(uint16_t session, uint32_t sequence) {
    logHook('K');
    ++tick_count;
    fakeInvalidatePairPrompt(session, sequence);
    if (!session || !sequence || prompt_visible) return;
    prompt_visible = true;
    prompt_session = session;
    prompt_sequence = sequence;
    ++prompt_show_count;
}
static void fakeNormalBoot() { logHook('B'); ++boot_count; }
static void fakeForceOff() { logHook('O'); ++force_off_count; }

namespace Crabit {
class CompanionTrace {
public:
    enum Event { ProbePongAccepted, Start, ProbeOnly, ManualBleOnly, Phase,
                 LegacyStop, BootBegin, BootEnd, RxPrepared, StartQueued, PowerOff,
                 UartProbeQueued, UartProbePass, UartProbeFailed, UartProbeTimeout,
                 BleStopRequested, BleOffComplete, BleResumeQueued };
    static void record(Event, uint16_t = 0, uint32_t = 0, uint32_t = 0) {}
};
}
namespace EventBus {
    bool tryPublish(const char* topic, const char* value, uint16_t id) {
        ++publish_count;
        snprintf(published_topic, sizeof(published_topic), "%s", topic ? topic : "");
        snprintf(published_value, sizeof(published_value), "%s", value ? value : "");
        published_id = id;
        return publish_result;
    }
}
class Uart {
public:
    static bool prepareAmebaRx() { return true; }
};
class CrabitLink {
public:
    static bool send(const char* topic, const char* value, uint16_t id = 0) {
        return EventBus::tryPublish(topic, value, id);
    }
};
namespace SysPower { void feed() { ++feed_count; } }
bool ameba_active;
bool amebaSessionActive() { return ameba_active; }
bool g_coproc_active;
uint32_t HAL_GetTick() { return now_ms; }

#include "companion_ble.h"

static Crabit::CompanionBle::UiHooks uiPorts() {
    Crabit::CompanionBle::UiHooks ui{};
    ui.frameAppActive = fakeFrameAppActive;
    ui.launchBleBackground = fakeLaunchBleBackground;
    ui.showToast = fakeShowToast;
    ui.dismissPairPrompt = fakeDismissPairPrompt;
    ui.invalidatePairPrompt = fakeInvalidatePairPrompt;
    ui.tickPairPrompt = fakeTickPairPrompt;
    return ui;
}
static Crabit::CompanionBle::HardwareHooks hardwarePorts() {
    Crabit::CompanionBle::HardwareHooks hardware{};
    hardware.normalBoot = fakeNormalBoot;
    hardware.forceOff = fakeForceOff;
    return hardware;
}

// Mirrors the existing prompt component's wiring: its ALLOW/DENY commands are
// the same service callbacks, so a rejected command keeps the prompt visible.
static bool pressAllow() {
    if (!prompt_visible) return false;
    if (!Crabit::CompanionBle::confirm(prompt_session, prompt_sequence)) return false;
    fakeDismissPairPrompt();
    return true;
}
static bool pressDeny() {
    if (!prompt_visible) return false;
    if (!Crabit::CompanionBle::cancel(prompt_session, prompt_sequence)) return false;
    fakeDismissPairPrompt();
    return true;
}

static void reset_environment() {
    now_ms = 0; force_off_count = feed_count = launch_count = toast_count = publish_count = 0;
    boot_count = frame_query_count = 0;
    publish_result = true; published_topic[0] = published_value[0] = '\0'; published_id = 0;
    prompt_visible = false; prompt_session = 0; prompt_sequence = 0;
    dismiss_count = invalidate_count = tick_count = prompt_show_count = 0;
    resetHookLog();
    GlobalStore.ble_status[0] = '\0';
    ameba_active = g_coproc_active = false;
    frame_active = frame_active_on_second_query = false;
    last_toast[0] = '\0';
}

static uint16_t start_mode_with_uart_pass() {
    resetHookLog();
    assert(Crabit::CompanionBle::start());
    // The FRAME owner query then the launch port fire synchronously in their
    // original positions; no hardware port runs on the launch path.
    assert(strcmp(hook_log, "FL") == 0);
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t id = published_id;
    assert(id != 0 && strcmp(published_topic, "sys/bw/ping") == 0 &&
           strcmp(published_value, "BLE_UART") == 0);
    Crabit::CompanionBle::receive(id, "UART_OK");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);
    Crabit::CompanionBle::receive(id, "CONNECTED");
    return id;
}

static void stop_mode_for_pair_test() {
    assert(Crabit::CompanionBle::stop());
    resetHookLog();
    Crabit::CompanionBle::tick();
    // Hardware power-off runs before the prompt tick in the original order.
    assert(hook_log[0] == 'O' && strchr(hook_log, 'K') != nullptr);
    now_ms += 250;
    Crabit::CompanionBle::tick();
    assert(strcmp(Crabit::CompanionBle::status(), "OFF") == 0);
    assert(!prompt_visible);
}

static void unbound_is_inert_and_bind_rejects_incomplete_ports() {
    reset_environment();
    assert(!Crabit::CompanionBle::bound());
    assert(!Crabit::CompanionBle::active() && !Crabit::CompanionBle::transitioning());
    assert(!Crabit::CompanionBle::hardwareActive() && !Crabit::CompanionBle::hardwareTransitioning());
    assert(strcmp(Crabit::CompanionBle::status(), "OFF") == 0);
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "DISABLED") == 0);
    assert(Crabit::CompanionBle::sessionId() == 0 && Crabit::CompanionBle::pairSequence() == 0);
    assert(!Crabit::CompanionBle::start());
    assert(!Crabit::CompanionBle::stop());
    assert(!Crabit::CompanionBle::toggle());
    assert(!Crabit::CompanionBle::confirm(1, 7));
    assert(!Crabit::CompanionBle::cancel(1, 7));
    Crabit::CompanionBle::resumeAfterUi();
    Crabit::CompanionBle::receive(1, "READY");
    Crabit::CompanionBle::receive(1, "PAIR_WAIT:7");
    for (unsigned i = 0; i < 5; ++i) {
        Crabit::CompanionBle::tick();
        now_ms += 5000;
    }
    // No port, hardware call, radio frame or fake boot/success happened.
    assert(launch_count == 0 && boot_count == 0 && force_off_count == 0);
    assert(toast_count == 0 && publish_count == 0 && feed_count == 0);
    assert(hook_log_len == 0 && invalidate_count == 0 && tick_count == 0);
    assert(!Crabit::CompanionBle::active() && !Crabit::CompanionBle::hardwareActive());
    assert(strcmp(Crabit::CompanionBle::status(), "OFF") == 0);

    // Each missing required port rejects the whole set and installs nothing.
    const Crabit::CompanionBle::UiHooks good = uiPorts();
    const Crabit::CompanionBle::HardwareHooks good_hw = hardwarePorts();
    Crabit::CompanionBle::UiHooks ui = good;
    ui.frameAppActive = nullptr;
    assert(!Crabit::CompanionBle::bind(ui, good_hw));
    ui = good; ui.launchBleBackground = nullptr;
    assert(!Crabit::CompanionBle::bind(ui, good_hw));
    ui = good; ui.showToast = nullptr;
    assert(!Crabit::CompanionBle::bind(ui, good_hw));
    ui = good; ui.dismissPairPrompt = nullptr;
    assert(!Crabit::CompanionBle::bind(ui, good_hw));
    ui = good; ui.invalidatePairPrompt = nullptr;
    assert(!Crabit::CompanionBle::bind(ui, good_hw));
    ui = good; ui.tickPairPrompt = nullptr;
    assert(!Crabit::CompanionBle::bind(ui, good_hw));
    assert(!Crabit::CompanionBle::bound());
    Crabit::CompanionBle::HardwareHooks hardware = good_hw;
    hardware.normalBoot = nullptr;
    assert(!Crabit::CompanionBle::bind(good, hardware));
    hardware = good_hw; hardware.forceOff = nullptr;
    assert(!Crabit::CompanionBle::bind(good, hardware));
    assert(!Crabit::CompanionBle::bound());
    assert(launch_count == 0 && boot_count == 0 && hook_log_len == 0);

    // Still inert after every rejected install.
    assert(!Crabit::CompanionBle::start());
    Crabit::CompanionBle::tick();
    assert(launch_count == 0 && boot_count == 0 && force_off_count == 0);

    assert(Crabit::CompanionBle::bind(good, good_hw));
    assert(Crabit::CompanionBle::bound());
    // One-time install: a second bind is refused and keeps the live ports.
    assert(!Crabit::CompanionBle::bind(good, good_hw));
    reset_environment();
}

static void mode_pair_prompt_and_ram_authentication() {
    reset_environment();
    assert(Crabit::CompanionBle::start());
    const uint16_t session_before_rebind = Crabit::CompanionBle::sessionId();
    assert(session_before_rebind != 0);
    assert(!Crabit::CompanionBle::bind(uiPorts(), hardwarePorts()));
    assert(Crabit::CompanionBle::sessionId() == session_before_rebind);
    assert(strcmp(Crabit::CompanionBle::status(), "STARTING") == 0);
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t gated_id = published_id;
    Crabit::CompanionBle::receive(gated_id, "CONNECTED");
    Crabit::CompanionBle::receive(gated_id, "PAIR_WAIT:39");
    Crabit::CompanionBle::tick();
    assert(!prompt_visible && !Crabit::CompanionBle::confirm(gated_id, 39));
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR WAIT") == 0);
    Crabit::CompanionBle::receive(gated_id, "UART_OK");
    Crabit::CompanionBle::tick();
    assert(prompt_visible && prompt_sequence == 39);
    stop_mode_for_pair_test();

    reset_environment();
    const uint16_t id = start_mode_with_uart_pass();
    const unsigned startup_publishes = publish_count;

    // UART PASS is only a transport gate: no prompt or pairing command is automatic.
    assert(!prompt_visible && strcmp(Crabit::CompanionBle::pairTransportStatus(),
                                     "UART PASS / RAM") == 0);
    assert(publish_count == startup_publishes);
    Crabit::CompanionBle::receive(uint16_t(id + 1), "PAIR_WAIT:41");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:0");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:41x");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:4294967296");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:41");
    // Invalidation is synchronous inside receive; only tick may publish prompt.
    assert(!prompt_visible);
    Crabit::CompanionBle::tick();
    assert(prompt_visible && prompt_session == id && prompt_sequence == 41);
    assert(prompt_show_count == 1 && publish_count == startup_publishes);

    // Only an explicit ALLOW publishes CONFIRM; wrong IDs/sequences and old ACKs
    // cannot confirm or authenticate the RAM token.
    assert(!Crabit::CompanionBle::confirm(uint16_t(id + 1), 41));
    assert(!Crabit::CompanionBle::confirm(id, 42));
    assert(!Crabit::CompanionBle::cancel(id, 42));
    Crabit::CompanionBle::receive(id, "PAIR_ACCEPTED:41");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR WAIT") == 0);
    assert(prompt_visible && publish_count == startup_publishes);
    assert(pressAllow());
    assert(!prompt_visible && strcmp(published_topic, "sys/bw/pair") == 0 &&
           strcmp(published_value, "CONFIRM:41") == 0 && published_id == id);
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "CONFIRM WAIT") == 0);
    Crabit::CompanionBle::receive(id, "AUTHENTICATED:40"); // stale sequence is not cached-token reconnect.
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "CONFIRM WAIT") == 0);
    Crabit::CompanionBle::receive(id, "AUTHENTICATED:41");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "RAM AUTHENTICATED") == 0);
    assert(!prompt_visible && publish_count == startup_publishes + 1);

    // A strictly newer authentication report is the cached-RAM-token reconnect case.
    Crabit::CompanionBle::receive(id, "AUTHENTICATED:42");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "RAM AUTHENTICATED") == 0);
    stop_mode_for_pair_test();

    // Queue rejection does not consume the prompt; a later explicit retry can.
    reset_environment();
    const uint16_t queue_id = start_mode_with_uart_pass();
    Crabit::CompanionBle::receive(queue_id, "PAIR_WAIT:51");
    Crabit::CompanionBle::tick();
    assert(prompt_visible);
    publish_result = false;
    assert(!pressAllow());
    assert(prompt_visible && strcmp(Crabit::CompanionBle::pairTransportStatus(),
                                    "PAIR WAIT") == 0);
    assert(toast_count == 1 && strcmp(last_toast, "PAIR SEND FAILED") == 0);
    publish_result = true;
    assert(pressAllow());
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "CONFIRM WAIT") == 0);
    Crabit::CompanionBle::receive(queue_id, "PAIR_TIMEOUT:51");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR TIMEOUT") == 0);
    now_ms += 2000;
    Crabit::CompanionBle::tick();
    assert(!prompt_visible);
    stop_mode_for_pair_test();

    reset_environment();
    const uint16_t deny_id = start_mode_with_uart_pass();
    Crabit::CompanionBle::receive(deny_id, "PAIR_WAIT:55");
    Crabit::CompanionBle::tick();
    assert(pressDeny());
    assert(strcmp(published_topic, "sys/bw/pair") == 0 &&
           strcmp(published_value, "CANCEL:55") == 0 && published_id == deny_id);
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "DENY WAIT") == 0);
    Crabit::CompanionBle::receive(deny_id, "PAIR_CANCELLED:55");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR DENIED") == 0);
    stop_mode_for_pair_test();

    // Duplicate challenge is inert; a new sequence replaces the old prompt.
    reset_environment();
    const uint16_t replace_id = start_mode_with_uart_pass();
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:60");
    Crabit::CompanionBle::tick();
    assert(prompt_visible && prompt_sequence == 60);
    const unsigned shown = prompt_show_count;
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:60");
    Crabit::CompanionBle::tick();
    assert(prompt_visible && prompt_sequence == 60 && prompt_show_count == shown);
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:61");
    Crabit::CompanionBle::tick();
    assert(prompt_visible && prompt_sequence == 61 && prompt_show_count == shown + 1);
    assert(!Crabit::CompanionBle::confirm(replace_id, 60));
    // DISCONNECTED invalidates the prompt synchronously; tick only runs after it.
    resetHookLog();
    Crabit::CompanionBle::receive(replace_id, "DISCONNECTED");
    assert(!prompt_visible && strcmp(hook_log, "D") == 0);
    Crabit::CompanionBle::tick();
    assert(strncmp(hook_log, "DK", 2) == 0);
    assert(!prompt_visible && Crabit::CompanionBle::pairSequence() == 0);
    assert(!Crabit::CompanionBle::confirm(replace_id, 61));
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);

    // Challenge expiry removes the prompt and rejects a late physical decision.
    Crabit::CompanionBle::receive(replace_id, "CONNECTED");
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:62");
    Crabit::CompanionBle::tick();
    assert(prompt_visible);
    now_ms += 30000;
    Crabit::CompanionBle::tick();
    assert(!prompt_visible && strcmp(Crabit::CompanionBle::pairTransportStatus(),
                                     "PAIR TIMEOUT") == 0);
    assert(!Crabit::CompanionBle::confirm(replace_id, 62));

    // STOP invalidates the challenge/session; a later start has a fresh ID and
    // cannot accept an old-session AUTH report.
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:63");
    Crabit::CompanionBle::tick();
    assert(prompt_visible);
    stop_mode_for_pair_test();
    const uint16_t fresh_id = start_mode_with_uart_pass();
    assert(fresh_id != replace_id);
    Crabit::CompanionBle::receive(replace_id, "AUTHENTICATED:64");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);
    stop_mode_for_pair_test();
}

static void background_adoption_and_status_observation() {
    reset_environment();
    strcpy(GlobalStore.ble_status, "CONNECTED"); // stale state is not startup proof
    Crabit::CompanionBle::receive(0, "READY");
    assert(Crabit::CompanionBle::start());
    assert(launch_count == 1 && boot_count == 0 && force_off_count == 0);
    assert(strcmp(Crabit::CompanionBle::status(), "STARTING") == 0);
    assert(Crabit::CompanionBle::active() && Crabit::CompanionBle::transitioning());
    assert(!Crabit::CompanionBle::start() && launch_count == 1);
    Crabit::CompanionBle::receive(0, "READY");
    Crabit::CompanionBle::receive(0, "CONNECTED");
    assert(strcmp(Crabit::CompanionBle::status(), "STARTING") == 0);
    assert(!Crabit::CompanionBle::adoptModeBle());
    // Actual sys/ble READY means connected, but alone is not startup evidence.
    Crabit::CompanionBle::observeModeStatus("READY");
    assert(!Crabit::CompanionBle::adoptModeBle());
    strcpy(GlobalStore.ble_status, "ADVERTISING");
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    // A single UI queue drain can overwrite the cache before FRAME executes.
    Crabit::CompanionBle::observeModeStatus("READY");
    strcpy(GlobalStore.ble_status, "READY");
    assert(Crabit::CompanionBle::adoptModeBle());
    assert(strcmp(Crabit::CompanionBle::status(), "CONNECTED") == 0);
    assert(Crabit::CompanionBle::active() && !Crabit::CompanionBle::transitioning());
    assert(!Crabit::CompanionBle::confirm(1, 1));
    assert(!Crabit::CompanionBle::cancel(1, 1));
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "DISABLED") == 0);
    assert(publish_count == 0); // UART probe is deferred until the first Running tick.
    const unsigned before_probe = publish_count;
    Crabit::CompanionBle::tick();
    assert(publish_count == before_probe + 1);
    assert(strcmp(published_topic, "sys/bw/ping") == 0 &&
           strcmp(published_value, "BLE_UART") == 0 && published_id != 0);
    const uint16_t probe_id = published_id;
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART WAIT") == 0);
    Crabit::CompanionBle::receive(uint16_t(probe_id + 1), "UART_OK");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART WAIT") == 0);
    Crabit::CompanionBle::receive(probe_id, "UART_OK");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);
    Crabit::CompanionBle::tick();
    assert(publish_count == before_probe + 1); // one-shot; never retries
    assert(Crabit::CompanionBle::active() && force_off_count == 0);
    const unsigned before_pair_api = publish_count;
    assert(!Crabit::CompanionBle::confirm(probe_id, 1));
    assert(!Crabit::CompanionBle::cancel(probe_id, 1));
    assert(publish_count == before_pair_api && toast_count == 0);
    Crabit::CompanionBle::observeModeStatus("DISCONNECTED"); Crabit::CompanionBle::tick();
    assert(strcmp(Crabit::CompanionBle::status(), "ADVERTISING") == 0);
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);
    assert(Crabit::CompanionBle::stop()); Crabit::CompanionBle::tick();
    now_ms = 250; Crabit::CompanionBle::tick();
}

static void start_guards_and_failure_release() {
    reset_environment(); ameba_active = true;
    assert(!Crabit::CompanionBle::start()); ameba_active = false;
    g_coproc_active = true; assert(!Crabit::CompanionBle::start()); g_coproc_active = false;
    frame_active = true; assert(!Crabit::CompanionBle::start()); frame_active = false;
    assert(launch_count == 0);
    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    Crabit::CompanionBle::observeModeStatus("FAILED");
    Crabit::CompanionBle::observeModeStatus("READY");
    assert(!Crabit::CompanionBle::adoptModeBle()); // Failure wins within the drain.
    Crabit::CompanionBle::finishModeBleStart("BLE START FAILED");
    assert(strcmp(Crabit::CompanionBle::status(), "BLE START FAILED") == 0);
    assert(!Crabit::CompanionBle::active() && !Crabit::CompanionBle::hardwareActive());
    assert(feed_count == 2); // startup reservation and failure release
}

static void resume_after_ui_waits_for_ownership_and_starts_once() {
    reset_environment();
    // The normal OFF path does not request a resume and must remain quiet.
    Crabit::CompanionBle::tick();
    Crabit::CompanionBle::tick();
    assert(launch_count == 0 && !Crabit::CompanionBle::active());

    // Model a previously active BLE session that the UI has stopped before
    // taking ownership. Resume must create a new background session, not reuse
    // the completed one.
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    const uint16_t previous_session = Crabit::CompanionBle::sessionId();
    stop_mode_for_pair_test();
    assert(launch_count == 1 && !Crabit::CompanionBle::active());

    Crabit::CompanionBle::resumeAfterUi();
    frame_active = true;
    Crabit::CompanionBle::tick();
    assert(launch_count == 1 && !Crabit::CompanionBle::active());
    frame_active = false;

    ameba_active = true;
    Crabit::CompanionBle::tick();
    assert(launch_count == 1 && !Crabit::CompanionBle::active());
    ameba_active = false;

    g_coproc_active = true;
    Crabit::CompanionBle::tick();
    assert(launch_count == 1 && !Crabit::CompanionBle::active());
    g_coproc_active = false;

    Crabit::CompanionBle::tick();
    assert(launch_count == 2 && Crabit::CompanionBle::active());
    const uint16_t resumed_session = Crabit::CompanionBle::sessionId();
    assert(resumed_session != 0 && resumed_session != previous_session);
    for (unsigned i = 0; i < 5; ++i) Crabit::CompanionBle::tick();
    assert(launch_count == 2 && Crabit::CompanionBle::sessionId() == resumed_session);

    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    stop_mode_for_pair_test();
}

static void resume_after_ui_does_not_restart_active_ble() {
    reset_environment();
    assert(Crabit::CompanionBle::start());
    const uint16_t original_session = Crabit::CompanionBle::sessionId();
    Crabit::CompanionBle::resumeAfterUi();
    Crabit::CompanionBle::tick();
    for (unsigned i = 0; i < 5; ++i) Crabit::CompanionBle::tick();
    assert(launch_count == 1 && Crabit::CompanionBle::active());
    assert(Crabit::CompanionBle::sessionId() == original_session);

    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    stop_mode_for_pair_test();
    Crabit::CompanionBle::tick();
    assert(launch_count == 1 && !Crabit::CompanionBle::active());
}

static void resume_after_ui_start_failure_is_reported_once() {
    reset_environment();
    Crabit::CompanionBle::resumeAfterUi();
    // Synthetic second-guard rejection exercises the one-shot error branch;
    // this is not evidence of an actual UI-task ownership race.
    frame_active_on_second_query = true;
    Crabit::CompanionBle::tick();
    assert(frame_query_count == 2);
    assert(launch_count == 0 && toast_count == 1);
    assert(strcmp(last_toast, "蓝牙恢复失败") == 0);

    for (unsigned i = 0; i < 5; ++i) Crabit::CompanionBle::tick();
    assert(launch_count == 0 && toast_count == 1);
}

static void stop_once_wait_wrap_and_restart() {
    reset_environment();
    assert(Crabit::CompanionBle::start());
    strcpy(GlobalStore.ble_status, "ADVERTISING");
    assert(!Crabit::CompanionBle::adoptModeBle()); // Cached text alone is not evidence.
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    assert(Crabit::CompanionBle::stop());
    assert(!Crabit::CompanionBle::stop());
    assert(Crabit::CompanionBle::transitioning());
    Crabit::CompanionBle::tick();
    assert(force_off_count == 1 && Crabit::CompanionBle::active());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    Crabit::CompanionBle::observeModeStatus("READY");
    assert(strcmp(Crabit::CompanionBle::status(), "STOPPING") == 0);
    now_ms = 249; Crabit::CompanionBle::tick();
    assert(force_off_count == 1 && Crabit::CompanionBle::active());
    now_ms = 250; Crabit::CompanionBle::tick();
    assert(!Crabit::CompanionBle::active() && strcmp(Crabit::CompanionBle::status(), "OFF") == 0);
    assert(GlobalStore.ble_status[0] == '\0');
    Crabit::CompanionBle::observeModeStatus("ADVERTISING"); // Off events are ignored.
    assert(Crabit::CompanionBle::start());
    assert(!Crabit::CompanionBle::adoptModeBle()); // Stop/off events cannot seed restart.
    strcpy(GlobalStore.ble_status, "ADVERTISING");
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    assert(Crabit::CompanionBle::stop());
    now_ms = UINT32_MAX - 100; Crabit::CompanionBle::tick();
    assert(force_off_count == 2);
    now_ms = 148; Crabit::CompanionBle::tick();
    assert(Crabit::CompanionBle::active());
    now_ms = 149; Crabit::CompanionBle::tick();
    assert(!Crabit::CompanionBle::active());
    assert(publish_count == 0); // neither adopted lifecycle reached a Running tick
    Crabit::CompanionBle::registerLegacyAutostart();
    assert(strcmp(Crabit::CompanionBle::status(), "LEGACY BLE ON") == 0);
    assert(!Crabit::CompanionBle::start() && toast_count == 1);
    assert(strcmp(last_toast, "STOP LEGACY BLE FIRST") == 0);
    assert(launch_count == 2);
    assert(Crabit::CompanionBle::stop());
    resetHookLog();
    Crabit::CompanionBle::tick();
    assert(force_off_count == 3 && strcmp(hook_log, "O") == 0);
    assert(publish_count == 0); // the legacy branch sends no radio STOP
    assert(!Crabit::CompanionBle::active());
}

static void uart_probe_terminal_results_and_restart_invalidation() {
    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    publish_result = false;
    Crabit::CompanionBle::tick();
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART SEND FAIL") == 0);
    Crabit::CompanionBle::tick();
    assert(publish_count == 1 && Crabit::CompanionBle::active() && force_off_count == 0);
    assert(Crabit::CompanionBle::stop()); Crabit::CompanionBle::tick();
    now_ms += 250; Crabit::CompanionBle::tick();

    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t old_id = published_id;
    Crabit::CompanionBle::receive(uint16_t(old_id + 1), "UART_OK");
    Crabit::CompanionBle::receive(old_id, "UNEXPECTED");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART WAIT") == 0);
    Crabit::CompanionBle::receive(old_id, "UART_NOT_READY");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART NOT READY") == 0);
    Crabit::CompanionBle::receive(old_id, "UART_OK"); // terminal result is immutable
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART NOT READY") == 0);
    assert(Crabit::CompanionBle::stop()); Crabit::CompanionBle::tick();
    now_ms += 250; Crabit::CompanionBle::tick();

    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t timeout_id = published_id;
    now_ms = 1999; Crabit::CompanionBle::tick();
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART WAIT") == 0);
    now_ms = 2000; Crabit::CompanionBle::tick();
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART TIMEOUT") == 0);
    Crabit::CompanionBle::receive(timeout_id, "UART_OK");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART TIMEOUT") == 0);
    for (unsigned i = 0; i < 25; ++i) Crabit::CompanionBle::tick();
    assert(publish_count == 1 && strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART TIMEOUT") == 0);
    assert(Crabit::CompanionBle::active() && force_off_count == 0);
    assert(Crabit::CompanionBle::stop()); Crabit::CompanionBle::tick();
    now_ms += 250; Crabit::CompanionBle::tick();

    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t stopped_id = published_id;
    assert(Crabit::CompanionBle::stop());
    const char* stopped_status = Crabit::CompanionBle::pairTransportStatus();
    Crabit::CompanionBle::receive(stopped_id, "UART_OK");
    assert(Crabit::CompanionBle::pairTransportStatus() == stopped_status);
    Crabit::CompanionBle::tick(); now_ms += 250; Crabit::CompanionBle::tick();
    assert(!Crabit::CompanionBle::active());
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t new_id = published_id;
    assert(new_id != stopped_id && publish_count == 2);
    Crabit::CompanionBle::receive(stopped_id, "UART_OK");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART WAIT") == 0);
    Crabit::CompanionBle::receive(new_id, "UART_OK");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);
    assert(Crabit::CompanionBle::stop()); Crabit::CompanionBle::tick();
    now_ms += 250; Crabit::CompanionBle::tick();

    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t failed_id = published_id;
    Crabit::CompanionBle::observeModeStatus("FAILED");
    assert(strcmp(Crabit::CompanionBle::status(), "BLE FAILED") == 0);
    const char* failed_probe_status = Crabit::CompanionBle::pairTransportStatus();
    Crabit::CompanionBle::receive(failed_id, "UART_OK");
    assert(Crabit::CompanionBle::pairTransportStatus() == failed_probe_status);
    Crabit::CompanionBle::tick();
    now_ms += 250;
    Crabit::CompanionBle::tick();
}

// Diagnostic probe build (CRABIT_COMPANION_UART_PROBE_ONLY=1): the proven
// background boot/PING route, no FRAME app launch, and the legacy autostart
// branch that must never send a radio command.
static void probe_route_and_legacy_paths() {
    reset_environment();
    assert(Crabit::CompanionBle::uartProbeOnly());
    resetHookLog();
    assert(Crabit::CompanionBle::start());
    assert(strcmp(hook_log, "F") == 0);
    assert(launch_count == 0 && boot_count == 0 && force_off_count == 0 && publish_count == 0);
    assert(strcmp(Crabit::CompanionBle::status(), "BOOTING") == 0);
    assert(Crabit::CompanionBle::active() && Crabit::CompanionBle::transitioning());
    assert(!Crabit::CompanionBle::start());
    resetHookLog();
    Crabit::CompanionBle::tick();
    assert(boot_count == 1 && hook_log[0] == 'B' && publish_count == 0);
    now_ms = 1220;
    resetHookLog();
    Crabit::CompanionBle::tick();
    assert(publish_count == 1 && strcmp(published_topic, "sys/bw/ping") == 0 &&
           strcmp(published_value, "PING") == 0 && published_id != 0);
    assert(strcmp(Crabit::CompanionBle::status(), "WAIT PONG") == 0);
    Crabit::CompanionBle::observeProbePong("BW16 PONG OK!");
    assert(strcmp(Crabit::CompanionBle::status(), "PONG SEEN") == 0);
    // The probe route never adopts radio status and never shows a pair prompt.
    const unsigned invalidations_before_receive = invalidate_count;
    resetHookLog();
    Crabit::CompanionBle::receive(published_id, "READY");
    assert(hook_log_len == 0 && !prompt_visible);
    assert(invalidate_count == invalidations_before_receive);
    // Stop uses no radio STOP frame; the existing deadline releases power once.
    assert(Crabit::CompanionBle::stop());
    const unsigned publishes_before_stop = publish_count;
    Crabit::CompanionBle::tick();
    assert(publish_count == publishes_before_stop && force_off_count == 0);
    now_ms += 2000;
    resetHookLog();
    Crabit::CompanionBle::tick();
    assert(force_off_count == 1 && publish_count == publishes_before_stop);
    assert(strchr(hook_log, 'O') != nullptr && publish_count == publishes_before_stop);
    assert(strcmp(Crabit::CompanionBle::status(), "PING STOPPED") == 0);
    assert(!Crabit::CompanionBle::active());

    // Legacy autostart stays tracked, blocks a second owner and stops via the
    // injected hardware port only.
    Crabit::CompanionBle::registerLegacyAutostart();
    assert(Crabit::CompanionBle::active() && Crabit::CompanionBle::hardwareActive());
    // The probe report intentionally masks the legacy label (unchanged policy).
    assert(strcmp(Crabit::CompanionBle::status(), "PING STOPPED") == 0);
    resetHookLog();
    assert(!Crabit::CompanionBle::start());
    assert(strcmp(last_toast, "STOP LEGACY BLE FIRST") == 0 && strcmp(hook_log, "FT") == 0);
    assert(Crabit::CompanionBle::stop());
    resetHookLog();
    Crabit::CompanionBle::tick();
    assert(force_off_count == 2 && strcmp(hook_log, "O") == 0);
    assert(publish_count == publishes_before_stop && !Crabit::CompanionBle::active());
}

int main() {
    unbound_is_inert_and_bind_rejects_incomplete_ports();
    if (Crabit::CompanionProbe::Enabled) {
        probe_route_and_legacy_paths();
    } else {
        background_adoption_and_status_observation();
        start_guards_and_failure_release();
        resume_after_ui_waits_for_ownership_and_starts_once();
        resume_after_ui_does_not_restart_active_ble();
        resume_after_ui_start_failure_is_reported_once();
        uart_probe_terminal_results_and_restart_invalidation();
        mode_pair_prompt_and_ram_authentication();
        stop_once_wait_wrap_and_restart();
    }
    puts("STM32 BLE background production assertions passed");
}
