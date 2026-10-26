#include <WiFi.h>
#undef min
#undef max
#include "src/product/app/wifi_hack_sniffer.h"

// build.ps1 stages byte-identical watch headers into the portable IDE sketch.
// Standalone BW16 ONLY. No OLED, STM32 protocol, BLE, connection or injection.
extern "C" unsigned char is_promisc_enabled(void);

volatile bool is_display_active = false;
WifiHackContext benchContext;
WifiHackContext* g_hackCtx = &benchContext;

namespace {
struct NativeRecord {
    char ssid[33];
    uint8_t bssid[6];
    int channel;
    int rssi;
};
NativeRecord nativeRecords[64];
size_t nativeCount = 0;
bool nativeDone = false;
bool nativeAccept = false;
bool receiveAccept = false;
uint32_t inFlight = 0;
uint32_t cycle = 0;
int channel24 = 9;  // Known nearby AP from V1 bench evidence; no native scan needed.
int channel5 = 40;
bool halted = false;

rtw_result_t nativeCallback(rtw_scan_handler_result_t* report) {
    if (!report)
        return RTW_SUCCESS;
    taskENTER_CRITICAL();
    if (nativeAccept) {
        if (report->scan_complete) {
            nativeAccept = false;
            nativeDone = true;
        } else if (nativeCount < 64) {
            const auto& ap = report->ap_details;
            auto& record = nativeRecords[nativeCount++];
            const size_t length = ap.SSID.len < 32 ? ap.SSID.len : 32;
            memcpy(record.ssid, ap.SSID.val, length);
            record.ssid[length] = 0;
            memcpy(record.bssid, ap.BSSID.octet, 6);
            record.channel = ap.channel;
            record.rssi = ap.signal_strength;
        }
    }
    taskEXIT_CRITICAL();
    return RTW_SUCCESS;
}

void receiveCallback(unsigned char* data, unsigned int length, void* metadata) {
    taskENTER_CRITICAL();
    const bool accepted = receiveAccept;
    if (accepted)
        ++inFlight;
    taskEXIT_CRITICAL();
    if (!accepted)
        return;
    master_promisc_callback(data, length, metadata);
    taskENTER_CRITICAL();
    --inFlight;
    taskEXIT_CRITICAL();
}

void halt(const char* reason) {
    halted = true;
    Serial.print("HALT reason=");
    Serial.println(reason);
}

bool stopReceiver() {
    taskENTER_CRITICAL();
    receiveAccept = false;
    taskEXIT_CRITICAL();
    Serial.println("RX_STOP_BEGIN");
    const int result = wifi_set_promisc(RTW_PROMISC_DISABLE, nullptr, 0);
    const uint32_t start = millis();
    uint32_t pending;
    do {
        taskENTER_CRITICAL();
        pending = inFlight;
        taskEXIT_CRITICAL();
        if (!pending)
            break;
        delay(1);
    } while (millis() - start < 2000);
    Serial.print("RX_STOP result=");
    Serial.print(result);
    Serial.print(" pending=");
    Serial.print(pending);
    Serial.print(" enabled=");
    Serial.println(is_promisc_enabled());
    if (result != 0 || pending || is_promisc_enabled()) {
        halt("receiver_not_quiescent");
        return false;
    }
    wifiSniffRxEnd();
    benchContext.currentMode = SNIFF_STOP;
    return true;
}

void printCounters(const char* stage, int requested) {
    const auto counts = wifiSniffRxSnapshot();
    // SDK 3.1.9 writes only the low byte through this int*, so initialize it.
    int actual = 0;
    const int get = wifi_get_channel(&actual);
    Serial.print(stage);
    Serial.print(" ms=");
    Serial.print(millis());
    Serial.print(" ch=");
    Serial.print(requested);
    Serial.print('/');
    Serial.print(actual);
    Serial.print(" get=");
    Serial.print(get);
    Serial.print(" enabled=");
    Serial.print(is_promisc_enabled());
    Serial.print(" cb=");
    Serial.print(counts.callbacks);
    Serial.print(" rx=");
    Serial.print(counts.frames);
    Serial.print(" beacon=");
    Serial.print(counts.beacons);
    Serial.print(" probe_response=");
    Serial.print(counts.probeResponses);
    Serial.print(" added=");
    Serial.println(counts.added);
}

void printList() {
    // Only called after stopReceiver: no concurrent vector/String access.
    Serial.print("PASSIVE_LIST count=");
    Serial.println(benchContext.scanResults.size());
    for (const auto& ap : benchContext.scanResults) {
        char mac[18];
        snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", ap.bssid[0], ap.bssid[1],
                 ap.bssid[2], ap.bssid[3], ap.bssid[4], ap.bssid[5]);
        Serial.print("PASSIVE_AP ch=");
        Serial.print(ap.channel);
        Serial.print(" bssid=");
        Serial.print(mac);
        Serial.print(" ssid=");
        Serial.println(ap.ssid);
    }
}

bool runNativeScan() {
    Serial.println("NATIVE_INIT_BEGIN");
    wifi_off();
    delay(50);
    const int on = wifi_on(RTW_MODE_STA);
    Serial.print("NATIVE_ON result=");
    Serial.println(on);
    if (on < 0) {
        halt("native_wifi_on");
        return false;
    }
    taskENTER_CRITICAL();
    nativeCount = 0;
    nativeDone = false;
    nativeAccept = true;
    taskEXIT_CRITICAL();
    const uint32_t start = millis();
    const int result = wifi_scan_networks(nativeCallback, nullptr);
    Serial.print("NATIVE_REQUEST result=");
    Serial.println(result);
    if (result != RTW_SUCCESS) {
        taskENTER_CRITICAL();
        nativeAccept = false;
        taskEXIT_CRITICAL();
        halt("native_scan_rejected");
        return false;
    }
    uint32_t last = start;
    while (true) {
        taskENTER_CRITICAL();
        const bool done = nativeDone;
        const size_t count = nativeCount;
        taskEXIT_CRITICAL();
        if (done)
            break;
        if (millis() - last >= 1000) {
            last = millis();
            Serial.print("NATIVE_WAIT ms=");
            Serial.print(last - start);
            Serial.print(" count=");
            Serial.println(count);
        }
        if (millis() - start >= 30000) {
            taskENTER_CRITICAL();
            nativeAccept = false;
            taskEXIT_CRITICAL();
            halt("native_completion_timeout_no_radio_restart");
            return false;
        }
        delay(10);
    }
    Serial.print("NATIVE_DONE ms=");
    Serial.print(millis() - start);
    Serial.print(" count=");
    Serial.println(nativeCount);
    int strongest24 = -1000, strongest5 = -1000;
    for (size_t i = 0; i < nativeCount; ++i) {
        const auto& ap = nativeRecords[i];
        Serial.print("NATIVE_AP ch=");
        Serial.print(ap.channel);
        Serial.print(" rssi=");
        Serial.print(ap.rssi);
        Serial.print(" ssid=");
        Serial.println(ap.ssid);
        if (ap.channel <= 14 && ap.rssi > strongest24) {
            strongest24 = ap.rssi;
            channel24 = ap.channel;
        } else if (ap.channel > 14 && ap.rssi > strongest5) {
            strongest5 = ap.rssi;
            channel5 = ap.channel;
        }
    }
    return true;
}

bool listenChannel(int channel, uint32_t dwell) {
    // Caller guarantees callbacks are stopped before changing parser state.
    wifiSniffRxBegin();
    benchContext.currentMode = SNIFF_ALL;
    benchContext.currentChannel = channel;
    const int set = wifi_set_channel(channel);
    int actual = 0;
    const int get = wifi_get_channel(&actual);
    if (get == 0)
        benchContext.currentChannel = actual;
    taskENTER_CRITICAL();
    receiveAccept = true;
    taskEXIT_CRITICAL();
    const int promisc = wifi_set_promisc(RTW_PROMISC_ENABLE_2, receiveCallback, 1);
    Serial.print("LISTEN_BEGIN ch=");
    Serial.print(channel);
    Serial.print(" actual=");
    Serial.print(actual);
    Serial.print(" set=");
    Serial.print(set);
    Serial.print(" get=");
    Serial.print(get);
    Serial.print(" pm=");
    Serial.println(promisc);
    const uint32_t start = millis();
    uint32_t last = start;
    while (millis() - start < dwell && set == 0 && promisc == 0) {
        if (millis() - last >= 1000) {
            last = millis();
            printCounters("RX_TICK", channel);
        }
        delay(10);
    }
    if (!stopReceiver())
        return false;
    printCounters("LISTEN_END", channel);
    Serial.print("LISTEN_SETUP_STATUS ");
    Serial.println(set == 0 && get == 0 && actual == channel && promisc == 0 ? "OK" : "FAILED");
    return true;
}

bool runPassive() {
    // Mirrors watch startScanCapture: off -> on(PROMISC) -> enter -> PM -> CH.
    Serial.println("PASSIVE_INIT_BEGIN");
    wifi_off();
    delay(50);
    const int on = wifi_on(RTW_MODE_PROMISC);
    delay(50);
    benchContext.reset();
    Serial.print("PASSIVE_ON result=");
    Serial.println(on);
    if (on < 0) {
        halt("promisc_wifi_on");
        return false;
    }
    wifi_enter_promisc_mode();
    // The watch enables capture before its first channel set. Reproduce it here.
    wifiSniffRxBegin();
    benchContext.currentMode = SNIFF_ALL;
    benchContext.currentChannel = 1;  // Do not reuse the previous sweep's final label.
    taskENTER_CRITICAL();
    receiveAccept = true;
    taskEXIT_CRITICAL();
    const int pm = wifi_set_promisc(RTW_PROMISC_ENABLE_2, receiveCallback, 1);
    const int set = wifi_set_channel(1);
    Serial.print("WATCH_ORDER pm=");
    Serial.print(pm);
    Serial.print(" set=");
    Serial.println(set);
    delay(2000);
    if (!stopReceiver())
        return false;
    printCounters("WATCH_ORDER_END", 1);
    if (!listenChannel(channel24, 8000) || !listenChannel(channel5, 8000))
        return false;
    printList();
    Serial.println("SWEEP_BEGIN same_watch_table_and_dwell");
    wifiSniffRxBegin();
    benchContext.currentMode = SNIFF_ALL;
    taskENTER_CRITICAL();
    receiveAccept = true;
    taskEXIT_CRITICAL();
    const int sweepPm = wifi_set_promisc(RTW_PROMISC_ENABLE_2, receiveCallback, 1);
    unsigned int sweepErrors = sweepPm == 0 ? 0 : 1;
    Serial.print("SWEEP_PROMISC result=");
    Serial.println(sweepPm);
    for (int i = 0; i < NUM_DEEP_CHANNELS; ++i) {
        const int channel = DEEP_SCAN_CHANNELS[i];
        const int setResult = wifi_set_channel(channel);
        int actual = 0;
        const int getResult = wifi_get_channel(&actual);
        if (setResult != 0 || getResult != 0 || actual != channel)
            ++sweepErrors;
        if (getResult == 0)
            benchContext.currentChannel = actual;
        Serial.print("SWEEP_CHANNEL set=");
        Serial.print(setResult);
        Serial.print(" get=");
        Serial.println(getResult);
        delay(channel <= 14 ? 800 : 500);
        printCounters("SWEEP_RX", channel);
    }
    if (!stopReceiver())
        return false;
    printList();
    Serial.print("SWEEP_DONE setup_errors=");
    Serial.println(sweepErrors);
    return true;
}
}  // namespace

void setup() {
    Serial.begin(115200);
    delay(1200);
    Serial.println("CRABIT_WIFI_SNIFF_BENCH_V2 passive_first receive_only");
    Serial.println("First cycle: passive only ch9/40. Later: native/fixed/sweep controls.");
}

void loop() {
    if (halted) {
        delay(1000);
        return;
    }
    Serial.print("CYCLE_BEGIN id=");
    Serial.println(++cycle);
    if (cycle == 1)
        Serial.println("COLD_PASSIVE_NO_NATIVE_SCAN");
    if ((cycle > 1 && !runNativeScan()) || !runPassive())
        return;
    Serial.print("CYCLE_COLLECTED_NOT_ACCEPTANCE id=");
    Serial.println(cycle);
    delay(5000);
}
