#ifndef WIFI_HACK_CORE_H
#define WIFI_HACK_CORE_H

#include <vector>
#include <Arduino.h> 
#include <WiFi.h>
#include <WiFiUdp.h>
#include "wifi_conf.h"     
#include "wifi_util.h"     
#include "../packet-injection.h"
#include "../AuditRelay.h"

extern volatile bool is_display_active; 
inline void safe_delay(unsigned long ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

enum SniffMode {
    SNIFF_ALL, SNIFF_BEACON, SNIFF_PROBE, SNIFF_DEAUTH, SNIFF_EAPOL, SNIFF_PWNAGOTCHI, SNIFF_STOP
};

struct WiFiResult { 
    String ssid; 
    uint8_t bssid[6]; 
    int channel; 
    bool selected = false; 
};

struct EapolLoot {
    String ap_mac;
    String sta_mac;
    String hex_data;
    String ssid;       // 核心补全：让战利品仓库能记住 WiFi 名字！
    bool is_synced; 
};

struct WifiHackContext {
    std::vector<WiFiResult> scanResults;
    std::vector<EapolLoot> lootPool; 
    
    std::vector<String> intelLogs;
    void addLog(String log) {
        if (intelLogs.size() >= 3) intelLogs.erase(intelLogs.begin());
        intelLogs.push_back(log);
    }

    void pushBleMessage(const String& msg) {
        AuditRelay::enqueue(msg.c_str());
    }

    String lastIntel = "Awaiting Signal...";
    uint32_t txCount = 0;
    uint32_t rxCount = 0;
    SniffMode currentMode = SNIFF_STOP;
    
    bool isHopping = false;
    unsigned long lastHopTime = 0;
    int currentChannel = 1;
    int currentChannelIndex = 0;
    bool use5GHz = false;

    void reset() {
        scanResults.clear(); 
        intelLogs.clear();
        lastIntel = "Awaiting Signal...";
        txCount = 0; rxCount = 0; 
        currentMode = SNIFF_STOP;
    }
};

extern WifiHackContext* g_hackCtx;

// 将信道配置放到全局核心库中，加 static 防止重复定义
static const int DEEP_SCAN_CHANNELS[] = {
    1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10,
    36, 40, 44, 48, 52, 56, 60, 64, 
    100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 
    149, 153, 157, 161, 165
};
static const int NUM_DEEP_CHANNELS = sizeof(DEEP_SCAN_CHANNELS) / sizeof(DEEP_SCAN_CHANNELS[0]);

static const unsigned long HOP_INTERVAL = 400; 
static const int CHANNELS_2GHZ[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
static const int CHANNELS_5GHZ[] = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};

inline bool isPwnagotchiMac(const uint8_t *mac) {
    const uint8_t pwnMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD};
    for (int i = 0; i < 6; i++) {
        if (mac[i] != pwnMac[i]) return false;
    }
    return true;
}

inline rtw_result_t appScanHandler(rtw_scan_handler_result_t *scan_result) {
    if (!g_hackCtx) return RTW_SUCCESS;
    rtw_scan_result_t *r = &scan_result->ap_details;
    if (scan_result->scan_complete == 0) {
        if (g_hackCtx->scanResults.size() >= 30) return RTW_SUCCESS;
        WiFiResult res;
        r->SSID.val[r->SSID.len] = 0; 
        res.ssid = String((const char *)r->SSID.val);
        if(res.ssid.length() == 0) res.ssid = "*HIDDEN*";
        res.channel = r->channel; 
        res.selected = false; 
        memcpy(res.bssid, r->BSSID.octet, 6);
        for(auto &item : g_hackCtx->scanResults) {
            if(memcmp(item.bssid, res.bssid, 6) == 0) return RTW_SUCCESS;
        }
        g_hackCtx->scanResults.push_back(res);
    }
    return RTW_SUCCESS;
}

#endif
