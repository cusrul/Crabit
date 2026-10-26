#ifndef WIFI_SNIFF_DIAGNOSTICS_H
#define WIFI_SNIFF_DIAGNOSTICS_H

#include <stdint.h>
#include <FreeRTOS.h>
#include <task.h>

// Only the sniffer's receive diagnostic enables these counters. No allocation,
// logging or Wi-Fi API calls are allowed while the driver callback records them.
struct WifiSniffRxSnapshot {
    bool active = false;
    uint32_t callbacks = 0;
    uint32_t frames = 0;
    uint32_t beacons = 0;
    uint32_t probeResponses = 0;
    uint32_t added = 0;
};

inline WifiSniffRxSnapshot& wifiSniffRxStorage() {
    static WifiSniffRxSnapshot counters;
    return counters;
}

inline void wifiSniffRxBegin() {
    taskENTER_CRITICAL();
    wifiSniffRxStorage() = WifiSniffRxSnapshot();
    wifiSniffRxStorage().active = true;
    taskEXIT_CRITICAL();
}

inline void wifiSniffRxEnd() {
    taskENTER_CRITICAL();
    wifiSniffRxStorage().active = false;
    taskEXIT_CRITICAL();
}

inline WifiSniffRxSnapshot wifiSniffRxSnapshot() {
    taskENTER_CRITICAL();
    const WifiSniffRxSnapshot snapshot = wifiSniffRxStorage();
    taskEXIT_CRITICAL();
    return snapshot;
}

inline void wifiSniffRxCallback() {
    taskENTER_CRITICAL();
    if (wifiSniffRxStorage().active)
        ++wifiSniffRxStorage().callbacks;
    taskEXIT_CRITICAL();
}

inline void wifiSniffRxFrame(uint8_t frameControl) {
    taskENTER_CRITICAL();
    WifiSniffRxSnapshot& counters = wifiSniffRxStorage();
    if (counters.active) {
        ++counters.frames;
        if ((frameControl & 0xfc) == 0x80)
            ++counters.beacons;
        if ((frameControl & 0xfc) == 0x50)
            ++counters.probeResponses;
    }
    taskEXIT_CRITICAL();
}

inline void wifiSniffRxAccessPoint() {
    taskENTER_CRITICAL();
    if (wifiSniffRxStorage().active)
        ++wifiSniffRxStorage().added;
    taskEXIT_CRITICAL();
}

#endif
