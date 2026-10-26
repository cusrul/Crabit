#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "companion_session.h"
#include "companion_probe.h"

struct TestGlobalStore { char ble_status[16]{}; };
TestGlobalStore GlobalStore;
static uint32_t now_ms;
static unsigned force_off_count, feed_count, launch_count, toast_count, publish_count;
static bool publish_result = true;
static char last_toast[64];
static char published_topic[24], published_value[24];
static uint16_t published_id;
static uint8_t launched_mode;
static bool pair_prompt_visible;
static uint16_t pair_prompt_session;
static uint32_t pair_prompt_sequence;
static unsigned pair_prompt_show_count, pair_prompt_dismiss_count;

namespace Crabit {
class CompanionTrace {
public:
    enum Event { ProbePongAccepted, Start, ProbeOnly, ManualBleOnly, Phase,
                 LegacyStop, BootBegin, BootEnd, RxPrepared, StartQueued, PowerOff,
                 UartProbeQueued, UartProbePass, UartProbeFailed, UartProbeTimeout,
                 BleStopRequested, BleOffComplete, BleResumeQueued };
    static void record(Event, uint16_t = 0, uint32_t = 0, uint32_t = 0) {}
};
class CompanionPairPrompt {
public:
    using Command = bool (*)(uint16_t, uint32_t);
    static CompanionPairPrompt* instance;
    CompanionPairPrompt(Command allow, Command deny) : allow_(allow), deny_(deny) { instance = this; }
    void dismiss() {
        if (pair_prompt_visible) ++pair_prompt_dismiss_count;
        pair_prompt_visible = false;
        pair_prompt_session = 0;
        pair_prompt_sequence = 0;
    }
    void invalidate(uint16_t session, uint32_t sequence) {
        if (!session || !sequence || session != pair_prompt_session ||
                sequence != pair_prompt_sequence) dismiss();
    }
    void tick(uint16_t session, uint32_t sequence) {
        invalidate(session, sequence);
        if (!session || !sequence || pair_prompt_visible) return;
        pair_prompt_visible = true;
        pair_prompt_session = session;
        pair_prompt_sequence = sequence;
        ++pair_prompt_show_count;
    }
    bool pressAllow() {
        if (!pair_prompt_visible || !allow_ ||
                !allow_(pair_prompt_session, pair_prompt_sequence)) return false;
        dismiss();
        return true;
    }
    bool pressDeny() {
        if (!pair_prompt_visible || !deny_ ||
                !deny_(pair_prompt_session, pair_prompt_sequence)) return false;
        dismiss();
        return true;
    }
private:
    Command allow_, deny_;
};
CompanionPairPrompt* CompanionPairPrompt::instance = nullptr;
class CyberLinkApp {
public:
    enum class Mode : uint8_t { Ui, Ble, BleBackground };
    explicit CyberLinkApp(Mode mode) { launched_mode = static_cast<uint8_t>(mode); }
};
}
namespace AppManager {
    bool frame_active;
    bool frame_active_on_second_query;
    unsigned frame_query_count;
    bool frameAppActive() {
        ++frame_query_count;
        return frame_active || (frame_active_on_second_query && frame_query_count == 2);
    }
    void launch(Crabit::CyberLinkApp* app) { ++launch_count; delete app; }
}
namespace Popup {
    void showToast(const char* message) {
        ++toast_count;
        snprintf(last_toast, sizeof(last_toast), "%s", message ? message : "");
    }
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
namespace SysPower { void feed() { ++feed_count; } }
bool ameba_active;
bool amebaSessionActive() { return ameba_active; }
bool g_coproc_active;
void BW16_ForceOff() { ++force_off_count; }
void BW16_NormalBoot() {}
bool STM32_PrepareAmebaUartRx() { return true; }
uint32_t HAL_GetTick() { return now_ms; }

#include "companion_ble.h"

static void reset_environment() {
    now_ms = 0; force_off_count = feed_count = launch_count = toast_count = publish_count = 0;
    publish_result = true; published_topic[0] = published_value[0] = '\0'; published_id = 0;
    launched_mode = static_cast<uint8_t>(Crabit::CyberLinkApp::Mode::Ui);
    pair_prompt_visible = false; pair_prompt_session = 0; pair_prompt_sequence = 0;
    pair_prompt_show_count = pair_prompt_dismiss_count = 0;
    GlobalStore.ble_status[0] = '\0';
    AppManager::frame_active = ameba_active = g_coproc_active = false;
    AppManager::frame_active_on_second_query = false;
    AppManager::frame_query_count = 0;
    last_toast[0] = '\0';
}

static uint16_t start_mode_with_uart_pass() {
    assert(Crabit::CompanionBle::start());
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
    Crabit::CompanionBle::tick();
    now_ms += 250;
    Crabit::CompanionBle::tick();
    assert(strcmp(Crabit::CompanionBle::status(), "OFF") == 0);
    assert(!pair_prompt_visible);
}

static void mode_pair_prompt_and_ram_authentication() {
    reset_environment();
    assert(Crabit::CompanionBle::start());
    Crabit::CompanionBle::observeModeStatus("ADVERTISING");
    assert(Crabit::CompanionBle::adoptModeBle());
    Crabit::CompanionBle::tick();
    const uint16_t gated_id = published_id;
    Crabit::CompanionBle::receive(gated_id, "CONNECTED");
    Crabit::CompanionBle::receive(gated_id, "PAIR_WAIT:39");
    Crabit::CompanionBle::tick();
    assert(!pair_prompt_visible && !Crabit::CompanionBle::confirm(gated_id, 39));
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR WAIT") == 0);
    Crabit::CompanionBle::receive(gated_id, "UART_OK");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible && pair_prompt_sequence == 39);
    stop_mode_for_pair_test();

    reset_environment();
    const uint16_t id = start_mode_with_uart_pass();
    const unsigned startup_publishes = publish_count;

    // UART PASS is only a transport gate: no prompt or pairing command is automatic.
    assert(!pair_prompt_visible && strcmp(Crabit::CompanionBle::pairTransportStatus(),
                                         "UART PASS / RAM") == 0);
    assert(publish_count == startup_publishes);
    Crabit::CompanionBle::receive(uint16_t(id + 1), "PAIR_WAIT:41");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:0");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:41x");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:4294967296");
    Crabit::CompanionBle::receive(id, "PAIR_WAIT:41");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible && pair_prompt_session == id && pair_prompt_sequence == 41);
    assert(pair_prompt_show_count == 1 && publish_count == startup_publishes);

    // Only an explicit ALLOW publishes CONFIRM; wrong IDs/sequences and old ACKs
    // cannot confirm or authenticate the RAM token.
    assert(!Crabit::CompanionBle::confirm(uint16_t(id + 1), 41));
    assert(!Crabit::CompanionBle::confirm(id, 42));
    assert(!Crabit::CompanionBle::cancel(id, 42));
    Crabit::CompanionBle::receive(id, "PAIR_ACCEPTED:41");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR WAIT") == 0);
    assert(pair_prompt_visible && publish_count == startup_publishes);
    assert(Crabit::CompanionPairPrompt::instance->pressAllow());
    assert(!pair_prompt_visible && strcmp(published_topic, "sys/bw/pair") == 0 &&
           strcmp(published_value, "CONFIRM:41") == 0 && published_id == id);
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "CONFIRM WAIT") == 0);
    Crabit::CompanionBle::receive(id, "AUTHENTICATED:40"); // stale sequence is not cached-token reconnect.
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "CONFIRM WAIT") == 0);
    Crabit::CompanionBle::receive(id, "AUTHENTICATED:41");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "RAM AUTHENTICATED") == 0);
    assert(!pair_prompt_visible && publish_count == startup_publishes + 1);

    // A strictly newer authentication report is the cached-RAM-token reconnect case.
    Crabit::CompanionBle::receive(id, "AUTHENTICATED:42");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "RAM AUTHENTICATED") == 0);
    stop_mode_for_pair_test();

    // Queue rejection does not consume the prompt; a later explicit retry can.
    reset_environment();
    const uint16_t queue_id = start_mode_with_uart_pass();
    Crabit::CompanionBle::receive(queue_id, "PAIR_WAIT:51");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible);
    publish_result = false;
    assert(!Crabit::CompanionPairPrompt::instance->pressAllow());
    assert(pair_prompt_visible && strcmp(Crabit::CompanionBle::pairTransportStatus(),
                                         "PAIR WAIT") == 0);
    publish_result = true;
    assert(Crabit::CompanionPairPrompt::instance->pressAllow());
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "CONFIRM WAIT") == 0);
    Crabit::CompanionBle::receive(queue_id, "PAIR_TIMEOUT:51");
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "PAIR TIMEOUT") == 0);
    now_ms += 2000;
    Crabit::CompanionBle::tick();
    assert(!pair_prompt_visible);
    stop_mode_for_pair_test();

    reset_environment();
    const uint16_t deny_id = start_mode_with_uart_pass();
    Crabit::CompanionBle::receive(deny_id, "PAIR_WAIT:55");
    Crabit::CompanionBle::tick();
    assert(Crabit::CompanionPairPrompt::instance->pressDeny());
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
    assert(pair_prompt_visible && pair_prompt_sequence == 60);
    const unsigned shown = pair_prompt_show_count;
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:60");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible && pair_prompt_sequence == 60 && pair_prompt_show_count == shown);
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:61");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible && pair_prompt_sequence == 61 && pair_prompt_show_count == shown + 1);
    assert(!Crabit::CompanionBle::confirm(replace_id, 60));
    Crabit::CompanionBle::observeModeStatus("DISCONNECTED");
    Crabit::CompanionBle::tick();
    assert(!pair_prompt_visible && Crabit::CompanionBle::pairSequence() == 0);
    assert(!Crabit::CompanionBle::confirm(replace_id, 61));
    assert(strcmp(Crabit::CompanionBle::pairTransportStatus(), "UART PASS / RAM") == 0);

    // Challenge expiry removes the prompt and rejects a late physical decision.
    Crabit::CompanionBle::receive(replace_id, "CONNECTED");
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:62");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible);
    now_ms += 30000;
    Crabit::CompanionBle::tick();
    assert(!pair_prompt_visible && strcmp(Crabit::CompanionBle::pairTransportStatus(),
                                          "PAIR TIMEOUT") == 0);
    assert(!Crabit::CompanionBle::confirm(replace_id, 62));

    // STOP invalidates the challenge/session; a later start has a fresh ID and
    // cannot accept an old-session AUTH report.
    Crabit::CompanionBle::receive(replace_id, "PAIR_WAIT:63");
    Crabit::CompanionBle::tick();
    assert(pair_prompt_visible);
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
    assert(launch_count == 1 && launched_mode == static_cast<uint8_t>(Crabit::CyberLinkApp::Mode::BleBackground));
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
    AppManager::frame_active = true; assert(!Crabit::CompanionBle::start()); AppManager::frame_active = false;
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
    AppManager::frame_active = true;
    Crabit::CompanionBle::tick();
    assert(launch_count == 1 && !Crabit::CompanionBle::active());
    AppManager::frame_active = false;

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
    assert(launched_mode == static_cast<uint8_t>(Crabit::CyberLinkApp::Mode::BleBackground));
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
    AppManager::frame_active_on_second_query = true;
    Crabit::CompanionBle::tick();
    assert(AppManager::frame_query_count == 2);
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
    assert(!Crabit::CompanionBle::start() && toast_count == 1);
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

int main() {
    background_adoption_and_status_observation();
    start_guards_and_failure_release();
    resume_after_ui_waits_for_ownership_and_starts_once();
    resume_after_ui_does_not_restart_active_ble();
    resume_after_ui_start_failure_is_reported_once();
    uart_probe_terminal_results_and_restart_invalidation();
    mode_pair_prompt_and_ram_authentication();
    stop_once_wait_wrap_and_restart();
    puts("STM32 BLE background production assertions passed");
}
