"""Execute the BW16 sniffer App header against narrow host-side SDK/UI mocks."""

from __future__ import annotations

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
APP_HEADER = ROOT / "firmware/rtl8720dn/NanoUI/app/app_wifi_sniff.h"
DIAGNOSTICS_HEADER = ROOT / "firmware/rtl8720dn/NanoUI/app/wifi_sniff_diagnostics.h"
SNIFFER_HEADER = ROOT / "firmware/rtl8720dn/NanoUI/app/wifi_hack_sniffer.h"

STRIPPED_IMPORTS = {
    '#include "../src/NanoUI.h"',
    '#include "wifi_hack_core.h"',
    '#include "wifi_hack_sniffer.h"',
    '#include "../packet-injection.h"',
    '#include "../NetManager.h"',
}


PRELUDE = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

namespace AuditRelay { inline void begin(){} inline void tick(){} }

std::mutex g_task_critical_mutex;
#include "@DIAGNOSTICS_HEADER@"

class String {
    std::string value_;
public:
    String() = default;
    String(const char* value) : value_(value ? value : "") {}
    const char* c_str() const { return value_.c_str(); }
    size_t length() const { return value_.length(); }
    String& operator+=(const char* value) { value_ += value ? value : ""; return *this; }
    String& operator+=(char value) { value_ += value; return *this; }
    friend String operator+(const String& left, const String& right) {
        String result(left.c_str());
        result.value_ += right.value_;
        return result;
    }
    friend String operator+(const char* left, const String& right) {
        String result(left);
        result.value_ += right.value_;
        return result;
    }
    friend String operator+(const String& left, const char* right) {
        String result(left.c_str());
        result.value_ += right ? right : "";
        return result;
    }
};

struct NanoCoordinate {
    int value;
    explicit NanoCoordinate(int input) : value(input) {}
};
struct ClipRect { NanoCoordinate left, top, right, bottom; };
enum { C_WHITE = 1 };

class Canvas {
public:
    std::vector<std::string> renderedText;
    void clear() {}
    void fillRect(int, int, int, int, int) {}
    void drawRect(int, int, int, int, int) {}
    void drawLine(int, int, int, int, int) {}
    void drawText(int, int, const char* text, int = 12, bool = false) {
        renderedText.push_back(text ? text : "");
    }
    void drawTextInRect(int, int, int, int, const char* text, int, int) {
        renderedText.push_back(text ? text : "");
    }
    void display() {}
};

class ScopedClip {
public:
    ScopedClip(Canvas*, ClipRect) {}
};

static int get_utf8_string_width(const char* text, int) {
    return text ? static_cast<int>(strlen(text)) * 6 : 0;
}
static int map(long value, long inMin, long inMax, long outMin, long outMax) {
    if (inMax == inMin) return static_cast<int>(outMin);
    return static_cast<int>(outMin + (value - inMin) * (outMax - outMin) / (inMax - inMin));
}

enum AppMode { APP_MODE_EXCLUSIVE };
class Application {
public:
    virtual ~Application() = default;
    virtual const char* getAppName() = 0;
    virtual AppMode getMode() = 0;
    virtual void runExclusive(Canvas*, class InputDriver*) = 0;
};

enum { EVT_NONE, EVT_BACK, EVT_UP, EVT_DOWN, EVT_OK };
struct UIEvent { int type; };
class AppWifiSniffer;
extern AppWifiSniffer* g_observed_app;

struct InputDriver {
    std::vector<int> events;
    size_t nextEvent = 0;
    UIEvent poll();
};

enum SniffMode {
    SNIFF_ALL,
    SNIFF_BEACON,
    SNIFF_PROBE,
    SNIFF_DEAUTH,
    SNIFF_EAPOL,
    SNIFF_PWNAGOTCHI,
    SNIFF_STOP
};

struct WiFiResult {
    String ssid;
    uint8_t bssid[6] = {};
    int channel = 1;
    bool selected = false;
};

struct WifiHackContext {
    std::vector<WiFiResult> scanResults;
    std::vector<String> intelLogs;
    std::vector<String> bleTxQueue;
    String lastIntel = "Awaiting Signal...";
    uint32_t txCount = 0;
    uint32_t rxCount = 0;
    SniffMode currentMode = SNIFF_STOP;
    bool isHopping = false;
    unsigned long lastHopTime = 0;
    int currentChannel = 1;
    int currentChannelIndex = 0;
    void reset() {
        scanResults.clear(); intelLogs.clear(); bleTxQueue.clear();
        lastIntel = "Awaiting Signal..."; txCount = 0; rxCount = 0;
        currentMode = SNIFF_STOP;
    }
    void addLog(String message) {
        if (intelLogs.size() >= 3) intelLogs.erase(intelLogs.begin());
        intelLogs.push_back(message);
    }
    void pushBleMessage(const String& message) {
        if (bleTxQueue.size() >= 15) bleTxQueue.erase(bleTxQueue.begin());
        bleTxQueue.push_back(message);
    }
};

static WifiHackContext g_context;
WifiHackContext* g_hackCtx = &g_context;
bool is_display_active = true;

static const int DEEP_SCAN_CHANNELS[] = {
    1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10,
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112,
    116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165
};
static const int NUM_DEEP_CHANNELS = sizeof(DEEP_SCAN_CHANNELS) / sizeof(DEEP_SCAN_CHANNELS[0]);

typedef int rtw_result_t;
enum { RTW_SUCCESS = 0, RTW_ERROR = -1 };
enum { RTW_MODE_STA = 1, RTW_MODE_PROMISC = 2 };
enum { RTW_PROMISC_DISABLE = 0, RTW_PROMISC_ENABLE_2 = 2 };

struct ScanSsid { uint8_t val[33] = {}; uint8_t len = 0; };
struct ScanBssid { uint8_t octet[6] = {}; };
struct rtw_scan_result_t {
    ScanSsid SSID;
    ScanBssid BSSID;
    int channel = 1;
};
struct rtw_scan_handler_result_t {
    int scan_complete = 0;
    void* user_data = NULL;
    rtw_scan_result_t ap_details;
};
typedef rtw_result_t (*ScanHandler)(rtw_scan_handler_result_t*);
typedef void (*PromiscCallback)(unsigned char*, unsigned int, void*);

enum MockScanBehavior { MOCK_SCAN_NEVER, MOCK_SCAN_COMPLETE_4440, MOCK_SCAN_STALE_THEN_COMPLETE };
struct ScheduledScanEvent {
    uint32_t due;
    bool complete;
    void* userData;
    ScanHandler handler;
    std::string ssid;
    int channel;
    uint8_t bssidSeed;
};

static uint32_t g_ticks = 0;
static MockScanBehavior g_scan_behavior = MOCK_SCAN_NEVER;
static std::vector<ScheduledScanEvent> g_scheduled_scan_events;
static ScanHandler g_last_scan_handler = NULL;
static void* g_last_scan_user_data = NULL;
static void* g_stale_scan_user_data = NULL;
static uint32_t g_scan_start_tick = 0;
static int g_scan_start_result = RTW_SUCCESS;
static int g_scan_start_calls = 0;
static int g_wifi_off_calls = 0;
static int g_wifi_on_result = 0;
static std::vector<int> g_wifi_on_modes;
static std::vector<size_t> g_scan_sizes_at_wifi_on;
static bool g_wifi_off_completes_scan = false;
static bool g_append_raced_ap_on_wifi_off = false;
static bool g_raced_ap_appended = false;
static int g_wifi_set_channel_result = 0;
static int g_wifi_get_channel_result = 0;
static int g_driver_channel = 11;
static bool g_follow_requested_channel = true;
static std::vector<int> g_channel_writes;
static int g_wifi_get_channel_calls = 0;
static int g_wifi_promisc_result = 0;
static unsigned char g_promisc_enabled = 0;
static int g_deauth_calls = 0;

static void deliver_scan_event(
    ScanHandler handler, void* token, bool complete, const char* ssid, int channel,
    uint8_t bssidSeed) {
    if (!handler) return;
    rtw_scan_handler_result_t result;
    result.scan_complete = complete ? 1 : 0;
    result.user_data = token;
    if (!complete) {
        const size_t length = strlen(ssid ? ssid : "");
        result.ap_details.SSID.len = static_cast<uint8_t>(length);
        memcpy(result.ap_details.SSID.val, ssid ? ssid : "", length);
        result.ap_details.SSID.val[length] = 0;
        result.ap_details.channel = channel;
        for (size_t i = 0; i < 6; ++i)
            result.ap_details.BSSID.octet[i] = static_cast<uint8_t>(bssidSeed + i);
    }
    handler(&result);
}

static void schedule_scan_event(
    uint32_t delay, bool complete, void* token, ScanHandler handler,
    const char* ssid, int channel, uint8_t bssidSeed) {
    ScheduledScanEvent event;
    event.due = g_ticks + delay;
    event.complete = complete;
    event.userData = token;
    event.handler = handler;
    event.ssid = ssid ? ssid : "";
    event.channel = channel;
    event.bssidSeed = bssidSeed;
    g_scheduled_scan_events.push_back(event);
}

static void dispatch_due_scan_events() {
    size_t index = 0;
    while (index < g_scheduled_scan_events.size()) {
        if (g_scheduled_scan_events[index].due > g_ticks) {
            ++index;
            continue;
        }
        const ScheduledScanEvent event = g_scheduled_scan_events[index];
        g_scheduled_scan_events.erase(g_scheduled_scan_events.begin() + index);
        deliver_scan_event(event.handler, event.userData, event.complete,
                           event.ssid.c_str(), event.channel, event.bssidSeed);
    }
}

static uint32_t hal_get_ticks() { return g_ticks; }
static void safe_delay(unsigned long milliseconds) {
    g_ticks += static_cast<uint32_t>(milliseconds);
    dispatch_due_scan_events();
}
static void yield() {}

static int wifi_on(int mode) {
    g_wifi_on_modes.push_back(mode);
    g_scan_sizes_at_wifi_on.push_back(g_hackCtx->scanResults.size());
    return g_wifi_on_result;
}
static void wifi_off() {
    ++g_wifi_off_calls;
    if (g_append_raced_ap_on_wifi_off && !g_raced_ap_appended) {
        WiFiResult raced;
        raced.ssid = String("raced-promisc-ap");
        raced.channel = 6;
        raced.bssid[0] = 0xee;
        g_hackCtx->scanResults.push_back(raced);
        g_raced_ap_appended = true;
    }
    if (g_wifi_off_completes_scan)
        deliver_scan_event(g_last_scan_handler, g_last_scan_user_data,
                           true, "", 0, 0);
}
static int wifi_set_channel(int channel) {
    g_channel_writes.push_back(channel);
    if (g_wifi_set_channel_result == 0 && g_follow_requested_channel)
        g_driver_channel = channel;
    return g_wifi_set_channel_result;
}
static int wifi_get_channel(int* channel) {
    ++g_wifi_get_channel_calls;
    if (g_wifi_get_channel_result == 0 && channel) {
        // Match the AmebaD int* API observed locally: only the low byte is written.
        *reinterpret_cast<uint8_t*>(channel) = static_cast<uint8_t>(g_driver_channel);
    }
    return g_wifi_get_channel_result;
}
static int wifi_set_promisc(int mode, PromiscCallback, int) {
    if (g_wifi_promisc_result == 0)
        g_promisc_enabled = mode == RTW_PROMISC_DISABLE ? 0 : 1;
    return g_wifi_promisc_result;
}
static void wifi_enter_promisc_mode() {}
static rtw_result_t wifi_scan_networks(ScanHandler handler, void* userData) {
    ++g_scan_start_calls;
    g_last_scan_handler = handler;
    g_last_scan_user_data = userData;
    g_scan_start_tick = g_ticks;
    if (g_scan_start_result != RTW_SUCCESS) return g_scan_start_result;

    if (g_scan_behavior == MOCK_SCAN_COMPLETE_4440) {
        schedule_scan_event(2200, false, userData, handler, "scan-network", 11, 0x10);
        schedule_scan_event(4440, true, userData, handler, "", 0, 0);
    } else if (g_scan_behavior == MOCK_SCAN_STALE_THEN_COMPLETE) {
        schedule_scan_event(1000, false, g_stale_scan_user_data, handler,
                            "stale-network", 36, 0x40);
        schedule_scan_event(1100, true, g_stale_scan_user_data, handler, "", 0, 0);
        schedule_scan_event(2000, false, userData, handler,
                            "current-network", 40, 0x70);
        schedule_scan_event(2440, true, userData, handler, "", 0, 0);
    }
    return RTW_SUCCESS;
}
static void master_promisc_callback(unsigned char*, unsigned int, void*) {}
static void init_eapol_queue() {}
static void process_raw_eapol_queue() {}
static void free_eapol_queue() {}
static void wifi_tx_broadcast_deauth(const uint8_t*, uint8_t, int, int) {
    ++g_deauth_calls;
    fprintf(stderr, "unexpected RF transmit path executed\n");
    abort();
}

#define private public
"""


POSTLUDE = r"""
#undef private

extern "C" unsigned char is_promisc_enabled(void) { return g_promisc_enabled; }

AppWifiSniffer* g_observed_app = NULL;
struct PollObservation {
    int state;
    int mode;
    bool hopping;
    size_t channelWriteCount;
    uint32_t tick;
};
static std::vector<PollObservation> g_poll_observations;

UIEvent InputDriver::poll() {
    if (g_observed_app) {
        PollObservation observation;
        observation.state = static_cast<int>(g_observed_app->state);
        observation.mode = static_cast<int>(g_hackCtx->currentMode);
        observation.hopping = g_hackCtx->isHopping;
        observation.channelWriteCount = g_channel_writes.size();
        observation.tick = g_ticks;
        g_poll_observations.push_back(observation);
    }
    if (nextEvent < events.size()) return UIEvent{events[nextEvent++]};
    return UIEvent{EVT_NONE};
}

static bool textEquals(const String& value, const char* expected) {
    return strcmp(value.c_str(), expected) == 0;
}
static void require(bool condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}
static void requireText(const char* actual, const char* expected, const char* message) {
    require(actual && strcmp(actual, expected) == 0, message);
}

static void resetMockState() {
    g_ticks = 0;
    g_scan_behavior = MOCK_SCAN_NEVER;
    g_scheduled_scan_events.clear();
    g_last_scan_handler = NULL;
    g_last_scan_user_data = NULL;
    g_stale_scan_user_data = NULL;
    g_scan_start_tick = 0;
    g_scan_start_result = RTW_SUCCESS;
    g_scan_start_calls = 0;
    g_wifi_off_calls = 0;
    g_wifi_on_result = 0;
    g_wifi_on_modes.clear();
    g_scan_sizes_at_wifi_on.clear();
    g_wifi_off_completes_scan = false;
    g_append_raced_ap_on_wifi_off = false;
    g_raced_ap_appended = false;
    g_wifi_set_channel_result = 0;
    g_wifi_get_channel_result = 0;
    g_driver_channel = 11;
    g_follow_requested_channel = true;
    g_channel_writes.clear();
    g_wifi_get_channel_calls = 0;
    g_wifi_promisc_result = 0;
    g_promisc_enabled = 0;
    g_deauth_calls = 0;
    g_poll_observations.clear();
    g_observed_app = NULL;
    is_display_active = true;
    g_hackCtx->reset();
    g_hackCtx->isHopping = false;
    g_hackCtx->currentChannel = 1;
    g_hackCtx->currentChannelIndex = 0;
    system_scan_accept_sniff = false;
    system_scan_complete_sniff = false;
    system_scan_count_sniff = 0;
    wifiSniffRxEnd();
}

static void deliverLateCurrentSession() {
    deliver_scan_event(g_last_scan_handler, g_last_scan_user_data,
                       false, "late-network", 149, 0xa0);
    deliver_scan_event(g_last_scan_handler, g_last_scan_user_data,
                       true, "", 0, 0);
}

static void scanSuccessScenario() {
    resetMockState();
    g_scan_behavior = MOCK_SCAN_COMPLETE_4440;
    g_append_raced_ap_on_wifi_off = true;
    WiFiResult existing;
    existing.ssid = String("preexisting");
    g_hackCtx->scanResults.push_back(existing);

    AppWifiSniffer app;
    Canvas canvas;
    InputDriver input;
    const bool completed = app.performSystemScan(&canvas, &input);
    require(completed, "SDK completion at 4440 ms was not accepted");
    require(g_ticks - g_scan_start_tick >= 4440 &&
                g_ticks - g_scan_start_tick < 15000,
            "system scan did not wait through the 3500 ms boundary");
    require(g_scan_start_calls == 1, "system scan was not started exactly once");
    require(g_raced_ap_appended, "mock did not exercise shutdown callback overlap");
    require(!g_scan_sizes_at_wifi_on.empty() && g_scan_sizes_at_wifi_on[0] == 0,
            "scan list was not cleared after radio shutdown and before STA start");
    require(g_hackCtx->scanResults.size() == 1 &&
                textEquals(g_hackCtx->scanResults[0].ssid, "scan-network"),
            "shutdown-race or old AP data survived the system scan");
    require(system_scan_count_sniff == 1, "accepted AP count is incorrect");
    require(!system_scan_accept_sniff && system_scan_complete_sniff,
            "completion callback did not close the scan session");
}

static void cancelAndTimeoutScenario() {
    resetMockState();
    g_wifi_off_completes_scan = true;
    AppWifiSniffer cancelApp;
    Canvas cancelCanvas;
    InputDriver cancelInput;
    cancelInput.events.push_back(EVT_BACK);
    require(!cancelApp.performSystemScan(&cancelCanvas, &cancelInput),
            "Back did not cancel system scan");
    requireText(cancelApp.scanFeedback, "SCAN CANCEL", "cancel feedback is incorrect");
    require(g_wifi_off_calls == 2, "cancel did not turn Wi-Fi off after scan startup");
    require(!system_scan_accept_sniff && !system_scan_complete_sniff,
            "synchronous wifi_off completion escaped cancellation rejection");
    deliverLateCurrentSession();
    require(!system_scan_complete_sniff && system_scan_count_sniff == 0 &&
                g_hackCtx->scanResults.empty(),
            "late callbacks changed cancelled scan state");

    resetMockState();
    g_wifi_off_completes_scan = true;
    AppWifiSniffer timeoutApp;
    Canvas timeoutCanvas;
    InputDriver timeoutInput;
    require(!timeoutApp.performSystemScan(&timeoutCanvas, &timeoutInput),
            "unfinished scan did not time out");
    requireText(timeoutApp.scanFeedback, "SCAN TIMEOUT", "timeout feedback is incorrect");
    require(g_ticks - g_scan_start_tick >= 15000,
            "timeout did not use the 15000 ms budget");
    require(g_wifi_off_calls == 2, "timeout did not turn Wi-Fi off after scan startup");
    require(!system_scan_accept_sniff && !system_scan_complete_sniff,
            "synchronous wifi_off completion escaped timeout rejection");
    deliverLateCurrentSession();
    require(!system_scan_complete_sniff && system_scan_count_sniff == 0 &&
                g_hackCtx->scanResults.empty(),
            "late callbacks changed timed-out scan state");
}

static void scanFailureScenario() {
    resetMockState();
    g_wifi_on_result = -1;
    AppWifiSniffer wifiOnApp;
    Canvas wifiOnCanvas;
    InputDriver wifiOnInput;
    require(!wifiOnApp.performSystemScan(&wifiOnCanvas, &wifiOnInput),
            "wifi_on failure was reported as scan success");
    requireText(wifiOnApp.scanFeedback, "SCAN FAILED", "wifi_on failure feedback is incorrect");
    require(g_scan_start_calls == 0, "scan started after wifi_on failed");

    resetMockState();
    g_scan_start_result = RTW_ERROR;
    AppWifiSniffer startApp;
    Canvas startCanvas;
    InputDriver startInput;
    require(!startApp.performSystemScan(&startCanvas, &startInput),
            "SDK scan-start failure was reported as success");
    requireText(startApp.scanFeedback, "SCAN FAILED", "scan-start failure feedback is incorrect");
    require(g_scan_start_calls == 1 && !system_scan_accept_sniff,
            "failed scan start left callback admission enabled");
    deliverLateCurrentSession();
    require(!system_scan_complete_sniff && system_scan_count_sniff == 0 &&
                g_hackCtx->scanResults.empty(),
            "callback after scan-start failure was accepted");
}

static void staleSessionScenario() {
    resetMockState();
    AppWifiSniffer app;
    Canvas canvas;
    InputDriver input;

    g_scan_behavior = MOCK_SCAN_COMPLETE_4440;
    require(app.performSystemScan(&canvas, &input), "first scan did not complete");
    void* oldToken = g_last_scan_user_data;
    require(oldToken != NULL, "first scan did not receive a session token");

    g_scan_behavior = MOCK_SCAN_STALE_THEN_COMPLETE;
    g_stale_scan_user_data = oldToken;
    require(app.performSystemScan(&canvas, &input), "second scan did not complete");
    require(g_last_scan_user_data != oldToken, "new scan reused the prior session token");
    require(g_hackCtx->scanResults.size() == 1 &&
                textEquals(g_hackCtx->scanResults[0].ssid, "current-network") &&
                g_hackCtx->scanResults[0].channel == 40,
            "stale scan callback contaminated the next scan result list");
    require(system_scan_count_sniff == 1,
            "stale callback changed the next scan's accepted result count");
}

static void channelScenario() {
    resetMockState();
    AppWifiSniffer app;
    g_follow_requested_channel = false;
    g_driver_channel = 40;
    app.setScanChannel(149);
    require(app.scanRequestedChannel == 149 && app.scanDriverChannel == 40 &&
                g_hackCtx->currentChannel == 40,
            "low-byte channel read did not preserve the actual driver channel");

    g_wifi_set_channel_result = -7;
    g_driver_channel = 6;
    app.setScanChannel(11);
    require(app.scanRequestedChannel == 11 && app.scanSetResult == -7 &&
                app.scanGetResult == 0 && app.scanDriverChannel == 6 &&
                g_hackCtx->currentChannel == 6,
            "failed channel request erased requested-versus-driver distinction");
    require(g_wifi_get_channel_calls == 2 && g_channel_writes.size() == 2,
            "setScanChannel did not execute its real set/read sequence");
}

static void runExclusiveScenario() {
    resetMockState();
    AppWifiSniffer app;
    Canvas canvas;
    InputDriver input;
    input.events.push_back(EVT_DOWN);  // Select the passive fixed-channel path.
    input.events.push_back(EVT_OK);
    for (int i = 0; i < 1200; ++i) input.events.push_back(EVT_NONE);
    input.events.push_back(EVT_OK);    // Diagnostic OK enters the AP list.
    input.events.push_back(EVT_OK);    // Select the list's task row.
    input.events.push_back(EVT_DOWN);
    input.events.push_back(EVT_DOWN);  // Receive-only task (menu index 2).
    input.events.push_back(EVT_OK);
    input.events.push_back(EVT_BACK);  // Leave receive-only task.
    input.events.push_back(EVT_BACK);  // Leave AP list.
    input.events.push_back(EVT_BACK);  // Exit the App.

    g_observed_app = &app;
    app.runExclusive(&canvas, &input);
    g_observed_app = NULL;

    size_t firstDiag = g_poll_observations.size();
    size_t lastDiag = 0;
    for (size_t i = 0; i < g_poll_observations.size(); ++i) {
        if (g_poll_observations[i].state == AppWifiSniffer::STATE_SCAN_DIAG) {
            if (firstDiag == g_poll_observations.size()) firstDiag = i;
            lastDiag = i;
        }
    }
    require(firstDiag < g_poll_observations.size() && lastDiag > firstDiag + 1000,
            "passive receive diagnostic did not remain open for the scripted wait");
    const size_t fixedChannelWrites = g_poll_observations[firstDiag].channelWriteCount;
    require(fixedChannelWrites == 1, "passive diagnostic did not select one initial channel");
    for (size_t i = firstDiag; i <= lastDiag; ++i)
        require(g_poll_observations[i].channelWriteCount == fixedChannelWrites,
                "passive diagnostic automatically hopped channels while waiting");
    require(g_poll_observations[lastDiag + 1].state == AppWifiSniffer::STATE_LIVE_RADAR,
            "diagnostic OK did not enter the AP list");

    bool restoredReceiveMode = false;
    for (size_t i = 1; i < g_poll_observations.size(); ++i) {
        const PollObservation& before = g_poll_observations[i - 1];
        const PollObservation& after = g_poll_observations[i];
        if (before.state == AppWifiSniffer::STATE_EXEC && before.mode == SNIFF_EAPOL &&
            after.state == AppWifiSniffer::STATE_LIVE_RADAR &&
            after.mode == SNIFF_ALL && !after.hopping) {
            restoredReceiveMode = true;
        }
    }
    require(restoredReceiveMode,
            "Back from receive-only task did not restore SNIFF_ALL without hopping");
    require(g_deauth_calls == 0, "receive-only event sequence reached an RF transmit stub");
}

static void scanDwellScenario(bool native) {
    resetMockState();
    AppWifiSniffer app;
    Canvas canvas;
    InputDriver input;
    if (native) g_scan_behavior = MOCK_SCAN_COMPLETE_4440;
    else input.events.push_back(EVT_DOWN);
    input.events.push_back(EVT_OK);
    if (!native) input.events.push_back(EVT_OK); // Diagnostic -> list only.
    for (int i = 0; i < 34000; ++i) input.events.push_back(EVT_NONE);
    input.events.push_back(EVT_BACK);
    input.events.push_back(EVT_BACK);
    g_observed_app = &app;
    app.runExclusive(&canvas, &input);
    g_observed_app = NULL;

    size_t previousWrites = 0;
    uint32_t previousTick = 0;
    uint32_t previousPollTick = 0;
    bool hadListPoll = false;
    int hops = 0;
    for (const auto& observation : g_poll_observations) {
        if (observation.state != AppWifiSniffer::STATE_LIVE_RADAR) continue;
        if (hadListPoll)
            require(observation.tick - previousPollTick <= 1,
                    "slower channel changes must not block input polling");
        previousPollTick = observation.tick;
        hadListPoll = true;
        if (previousWrites == 0) {
            previousWrites = observation.channelWriteCount;
            previousTick = observation.tick;
        } else if (observation.channelWriteCount != previousWrites) {
            const uint32_t elapsed = observation.tick - previousTick;
            require(elapsed >= 2000 && elapsed <= 2002,
                    "list sweep must dwell two seconds per channel without blocking input");
            require(observation.channelWriteCount == previousWrites + 1,
                    "list sweep skipped channel writes");
            previousWrites = observation.channelWriteCount;
            previousTick = observation.tick;
            ++hops;
        }
    }
    require(hops >= 15, "test did not reach the 5GHz portion of the table");
    require(g_deauth_calls == 0, "scan dwell test entered a transmit task");
    require(g_scan_start_calls == (native ? 1 : 0), "native/passive entry path changed");
}

static void failedInitFooterScenario() {
    resetMockState();
    g_wifi_on_result = -1;
    AppWifiSniffer app;
    Canvas canvas;
    InputDriver input;
    input.events.push_back(EVT_DOWN);
    input.events.push_back(EVT_OK);
    input.events.push_back(EVT_BACK);
    input.events.push_back(EVT_BACK);
    g_observed_app = &app;
    app.runExclusive(&canvas, &input);
    g_observed_app = NULL;

    bool foundRetryFooter = false;
    for (size_t i = 0; i < canvas.renderedText.size(); ++i)
        if (canvas.renderedText[i] == "BACK RETRY") foundRetryFooter = true;
    require(foundRetryFooter, "failed receive initialization did not render BACK RETRY");
    require(g_deauth_calls == 0, "failed-init scenario reached an RF transmit stub");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    if (strcmp(argv[1], "scan-success") == 0) scanSuccessScenario();
    else if (strcmp(argv[1], "cancel-timeout") == 0) cancelAndTimeoutScenario();
    else if (strcmp(argv[1], "scan-failure") == 0) scanFailureScenario();
    else if (strcmp(argv[1], "stale-session") == 0) staleSessionScenario();
    else if (strcmp(argv[1], "channel") == 0) channelScenario();
    else if (strcmp(argv[1], "run-exclusive") == 0) runExclusiveScenario();
    else if (strcmp(argv[1], "dwell-passive") == 0) scanDwellScenario(false);
    else if (strcmp(argv[1], "dwell-native") == 0) scanDwellScenario(true);
    else if (strcmp(argv[1], "failed-footer") == 0) failedInitFooterScenario();
    else return 2;
    return 0;
}
"""


def app_header_without_mocked_imports(source: str) -> str:
    """Remove only the explicitly mocked top-level import lines; retain every body."""
    seen: set[str] = set()
    output: list[str] = []
    for line in source.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("#include") and stripped in STRIPPED_IMPORTS:
            seen.add(stripped)
            continue
        if stripped.startswith("#include") and stripped != "#include <vector>":
            raise AssertionError(f"unreviewed App import boundary: {stripped}")
        output.append(line)
    if seen != STRIPPED_IMPORTS:
        raise AssertionError(f"expected App imports missing: {sorted(STRIPPED_IMPORTS - seen)}")
    return "".join(output)


def callback_hook_order(source: str) -> list[str]:
    marker = "inline void master_promisc_callback("
    start = source.index(marker)
    closing = re.search(r"(?m)^}\s*$", source[start:])
    if closing is None:
        raise AssertionError("could not locate master_promisc_callback closing brace")
    body = source[start : start + closing.end()]
    hooks = (
        "wifiSniffRxCallback();",
        "if (!g_hackCtx || g_hackCtx->currentMode == SNIFF_STOP) return;",
        "if (!buf || len < 24) return;",
        "wifiSniffRxFrame(buf[0]);",
        "g_hackCtx->rxCount++;",
    )
    positions = [body.index(hook) for hook in hooks]
    if positions != sorted(positions):
        raise AssertionError("promiscuous callback diagnostics hooks changed order")
    return list(hooks)


class WifiSniffScanFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which("g++")
        if compiler is None:
            raise unittest.SkipTest("host g++ unavailable; no toolchain installation attempted")

        cls._temporary = tempfile.TemporaryDirectory(prefix="wifi-sniff-scan-flow-")
        fixture = Path(cls._temporary.name)
        mock_headers = fixture / "include"
        mock_headers.mkdir()
        (mock_headers / "FreeRTOS.h").write_text("#pragma once\n", encoding="utf-8")
        (mock_headers / "task.h").write_text(
            "#pragma once\n"
            "#include <mutex>\n"
            "extern std::mutex g_task_critical_mutex;\n"
            "#define taskENTER_CRITICAL() g_task_critical_mutex.lock()\n"
            "#define taskEXIT_CRITICAL() g_task_critical_mutex.unlock()\n",
            encoding="utf-8",
        )

        app_source = app_header_without_mocked_imports(APP_HEADER.read_text(encoding="utf-8"))
        driver_source = (
            PRELUDE.replace("@DIAGNOSTICS_HEADER@", DIAGNOSTICS_HEADER.as_posix())
            + app_source
            + POSTLUDE
        )
        driver = fixture / "wifi_sniff_scan_flow_host.cpp"
        driver.write_text(driver_source, encoding="utf-8")
        cls._executable = fixture / "wifi_sniff_scan_flow_host.exe"

        result = subprocess.run(
            [
                compiler,
                "-std=c++11",
                "-O0",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-pthread",
                "-I",
                str(mock_headers),
                str(driver),
                "-o",
                str(cls._executable),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        if result.returncode != 0:
            cls._temporary.cleanup()
            raise AssertionError(
                "host compilation of the complete App header failed:\n"
                f"{result.stdout}{result.stderr}"
            )

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, "_temporary"):
            cls._temporary.cleanup()

    def run_scenario(self, name: str) -> None:
        result = subprocess.run(
            [str(self._executable), name],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
        self.assertEqual(
            result.returncode,
            0,
            f"scenario {name!r} failed:\n{result.stdout}{result.stderr}",
        )

    def test_system_scan_waits_for_sdk_completion_after_old_timer_boundary(self):
        self.run_scenario("scan-success")

    def test_cancel_and_timeout_disable_acceptance_before_wifi_off_and_late_callbacks(self):
        self.run_scenario("cancel-timeout")

    def test_wifi_on_and_sdk_scan_start_failures(self):
        self.run_scenario("scan-failure")

    def test_next_scan_rejects_stale_user_data_token(self):
        self.run_scenario("stale-session")

    def test_scan_channel_tracks_low_byte_driver_read_and_failed_request(self):
        self.run_scenario("channel")

    def test_run_exclusive_passive_diagnostic_list_and_receive_back_flow(self):
        self.run_scenario("run-exclusive")

    def test_failed_receive_initialization_shows_retry_footer(self):
        self.run_scenario("failed-footer")

    def test_passive_list_uses_two_second_dwell_on_both_bands(self):
        self.run_scenario("dwell-passive")

    def test_native_list_uses_two_second_dwell_on_both_bands(self):
        self.run_scenario("dwell-native")

    def test_promiscuous_diagnostic_hook_order(self):
        order = callback_hook_order(SNIFFER_HEADER.read_text(encoding="utf-8"))
        self.assertEqual(len(order), 5)


if __name__ == "__main__":
    unittest.main()
