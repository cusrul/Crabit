#ifndef WIFI_HACK_SNIFFER_H
#define WIFI_HACK_SNIFFER_H

#include "wifi_hack_core.h"
#include "wifi_sniff_diagnostics.h"
#include <BLEDevice.h>
#include "../NetManager.h" 

#define RAW_EAPOL_QUEUE_SIZE 10
#define MAX_RAW_PACKET_LEN 300 

typedef struct {
    uint16_t len;
    uint8_t buf[MAX_RAW_PACKET_LEN];
} RawEapolPacket;

inline volatile RawEapolPacket** get_eapol_queue_ptr() { static volatile RawEapolPacket* q = NULL; return &q; }
inline volatile int* get_eapol_head_ptr() { static volatile int h = 0; return &h; }
inline volatile int* get_eapol_tail_ptr() { static volatile int t = 0; return &t; }

inline void init_eapol_queue() {
    volatile RawEapolPacket** q = get_eapol_queue_ptr();
    if (*q == NULL) *q = (RawEapolPacket*)malloc(sizeof(RawEapolPacket) * RAW_EAPOL_QUEUE_SIZE);
    *get_eapol_head_ptr() = 0;
    *get_eapol_tail_ptr() = 0;
}

inline void free_eapol_queue() {
    volatile RawEapolPacket** q = get_eapol_queue_ptr();
    if (*q != NULL) { free((void*)*q); *q = NULL; }
}

inline void process_raw_eapol_queue() {
    volatile RawEapolPacket* queue = *get_eapol_queue_ptr();
    volatile int* p_head = get_eapol_head_ptr();
    volatile int* p_tail = get_eapol_tail_ptr();
    
    if (queue == NULL) return;

    while (*p_tail != *p_head) {
        volatile RawEapolPacket* pkt = &queue[*p_tail];
        uint16_t len = pkt->len;
        
        const uint8_t *addr1 = (const uint8_t *)&pkt->buf[4];  
        const uint8_t *addr2 = (const uint8_t *)&pkt->buf[10]; 
        const uint8_t *addr3 = (const uint8_t *)&pkt->buf[16]; 
        
        uint8_t to_ds = pkt->buf[1] & 0x01;
        uint8_t from_ds = (pkt->buf[1] & 0x02) >> 1;

        const uint8_t* true_bssid;
        const uint8_t* true_sta;

        if (to_ds == 1 && from_ds == 0) {
            true_bssid = addr1;
            true_sta = addr2;
        } else if (to_ds == 0 && from_ds == 1) {
            true_sta = addr1;
            true_bssid = addr2;
        } else {
            true_bssid = addr3;
            true_sta = addr2;
        }

        String found_ssid = "UNKNOWN";
        if (g_hackCtx) {
            for (auto &ap : g_hackCtx->scanResults) {
                if (memcmp(ap.bssid, true_bssid, 6) == 0) {
                    found_ssid = ap.ssid; 
                    break;
                }
            }
            if (found_ssid == "UNKNOWN") {
                for (auto &ap : g_hackCtx->scanResults) {
                    if (memcmp(ap.bssid, true_bssid, 5) == 0) {
                        found_ssid = ap.ssid; 
                        break;
                    }
                }
            }
        }

        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", true_bssid[0], true_bssid[1], true_bssid[2], true_bssid[3], true_bssid[4], true_bssid[5]);
        String ap_mac = String(mac_str); 
        
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", true_sta[0], true_sta[1], true_sta[2], true_sta[3], true_sta[4], true_sta[5]);
        String sta_mac = String(mac_str);

        int current_ap_packet_count = 0;
        if(g_hackCtx) {
            for (auto &loot : g_hackCtx->lootPool) {
                if (loot.ap_mac == ap_mac || loot.ap_mac == sta_mac) current_ap_packet_count++;
            }
        }

        if (current_ap_packet_count < 8) {
            if(g_hackCtx) g_hackCtx->addLog("[握手] " + (found_ssid == "UNKNOWN" ? ap_mac.substring(9) : found_ssid) + " (" + String(current_ap_packet_count + 1) + "/8)");

            String hexData = ""; hexData.reserve(len * 2 + 1);
            const char hex_chars[] = "0123456789ABCDEF";
            for (int j = 0; j < len; j++) { hexData += hex_chars[(pkt->buf[j] >> 4) & 0x0F]; hexData += hex_chars[pkt->buf[j] & 0x0F]; }

            if(g_hackCtx) {
                // 洗除异常字符，避免 JSON 解析断裂
                String clean_ssid = "";
                for(int k=0; k<found_ssid.length(); k++){
                    char c = found_ssid[k];
                    if(c >= 32 && c <= 126 && c != '"' && c != '\\') clean_ssid += c;
                    else clean_ssid += '_';
                }

                String eapol_json = "{\"t\":\"wf/eapol\",\"s\":\"" + ap_mac + "\",\"d\":\"" + sta_mac + "\",\"p\":\"" + hexData + "\",\"n\":\"" + clean_ssid + "\"}";
                g_hackCtx->pushBleMessage(eapol_json);

                EapolLoot newLoot;
                newLoot.ap_mac = ap_mac; 
                newLoot.sta_mac = sta_mac; 
                newLoot.hex_data = hexData; 
                newLoot.ssid = clean_ssid; // 同步存储 SSID
                newLoot.is_synced = false;
                
                if (g_hackCtx->lootPool.size() >= 50 ) g_hackCtx->lootPool.erase(g_hackCtx->lootPool.begin());
                g_hackCtx->lootPool.push_back(newLoot);
            }
        }

        *p_tail = (*p_tail + 1) % RAW_EAPOL_QUEUE_SIZE;
    }
}

inline void master_promisc_callback(unsigned char *buf, unsigned int len, void* param) {
    wifiSniffRxCallback();
    (void)param; if (!g_hackCtx || g_hackCtx->currentMode == SNIFF_STOP) return;
    if (!buf || len < 24) return;
    wifiSniffRxFrame(buf[0]);
    g_hackCtx->rxCount++;
    uint16_t fc = buf[0] | (buf[1] << 8); uint8_t ftype = (fc & 0x0C) >> 2; uint8_t fsubtype = (fc & 0xF0) >> 4;

    if (ftype == 0 && (fsubtype == 8 || fsubtype == 5)) {
        if (g_hackCtx->currentMode == SNIFF_ALL || g_hackCtx->currentMode == SNIFF_BEACON || g_hackCtx->currentMode == SNIFF_PWNAGOTCHI) {
            const uint8_t* bssid = &buf[16]; char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X", bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
            if (isPwnagotchiMac(bssid)) g_hackCtx->addLog("[伙伴] " + String(mac_str).substring(9));

            String ssid = ""; int offset = 36; 
            if (offset + 1 < len && buf[offset] == 0) {
                int tagLen = buf[offset+1];
                if (offset + 2 + tagLen <= len && tagLen > 0 && tagLen <= 32) {
                    for (int i=0; i<tagLen; i++) { char c = buf[offset + 2 + i]; ssid += (c >= 32 && c <= 126) ? c : '.'; }
                }
            }
            if (ssid.length() == 0) ssid = "*HIDDEN*";
            
            if (g_hackCtx->currentMode == SNIFF_ALL && g_hackCtx->scanResults.size() < 100) {
                bool exists = false; for (auto &ap : g_hackCtx->scanResults) { if (memcmp(ap.bssid, bssid, 6) == 0) { exists = true; break; } }
                if (!exists) { WiFiResult res; res.ssid = ssid; res.channel = g_hackCtx->currentChannel; res.selected = false; memcpy(res.bssid, bssid, 6); g_hackCtx->scanResults.push_back(res); wifiSniffRxAccessPoint(); }
            }
            if (g_hackCtx->currentMode == SNIFF_BEACON && fsubtype == 8) g_hackCtx->addLog("[信标] " + ssid);
        }
    }
    else if (ftype == 0 && fsubtype == 4) {
        if (g_hackCtx->currentMode == SNIFF_ALL || g_hackCtx->currentMode == SNIFF_PROBE) {
            String ssid = ""; int offset = 24;
            while (offset < len - 2) {
                if (buf[offset] == 0) {
                    int tagLen = buf[offset+1];
                    if (tagLen == 0) ssid = "<BCAST>";
                    else if (offset + 2 + tagLen <= len) { for (int i=0; i<tagLen; i++) { char c = buf[offset+2+i]; ssid += (c >= 32 && c <= 126) ? c : '.'; } }
                    break;
                }
                offset += 2 + buf[offset+1];
            }
            if (ssid.length() > 0) {
                const uint8_t* sa = &buf[10]; char mac_short[9]; snprintf(mac_short, sizeof(mac_short), "%02X:%02X:%02X", sa[3], sa[4], sa[5]);
                g_hackCtx->addLog("[探测] " + String(mac_short) + " > " + ssid);
                String escaped_ssid = ssid;
                escaped_ssid.replace("\\", "\\\\");
                escaped_ssid.replace("\"", "\\\"");
                String probe_json = "{\"t\":\"wf/prb\",\"m\":\"" + String(mac_short) + "\",\"s\":\"" + escaped_ssid + "\"}";
                g_hackCtx->pushBleMessage(probe_json);
            }
        }
    }
    else if (ftype == 0 && (fsubtype == 12 || fsubtype == 10)) {
        if (g_hackCtx->currentMode == SNIFF_ALL || g_hackCtx->currentMode == SNIFF_DEAUTH) {
            const char* type_str = (fsubtype == 12) ? "断连" : "离线"; const uint8_t* sa = &buf[10]; const uint8_t* da = &buf[4]; char m[18];
            snprintf(m, 18, "%02X:%02X>%02X:%02X", sa[4], sa[5], da[4], da[5]); g_hackCtx->addLog("[" + String(type_str) + "] " + String(m));
        }
    }
    else if (ftype == 2) {
        if (g_hackCtx->currentMode == SNIFF_ALL || g_hackCtx->currentMode == SNIFF_EAPOL) {
            bool is_eapol = false; 
            if (len >= 36 && buf[24] == 0xAA && buf[25] == 0xAA && buf[30] == 0x88 && buf[31] == 0x8E) is_eapol = true; 
            else if (len >= 38 && (buf[0] & 0x0F) == 0x08 && buf[26] == 0xAA && buf[27] == 0xAA && buf[32] == 0x88 && buf[33] == 0x8E) is_eapol = true; 

            if (is_eapol) {
                volatile RawEapolPacket* queue = *get_eapol_queue_ptr();
                volatile int* p_head = get_eapol_head_ptr(); volatile int* p_tail = get_eapol_tail_ptr();
                if (queue != NULL) {
                    int next_head = (*p_head + 1) % RAW_EAPOL_QUEUE_SIZE;
                    if (next_head != *p_tail) { 
                        int copy_len = (len < MAX_RAW_PACKET_LEN) ? len : MAX_RAW_PACKET_LEN; queue[*p_head].len = copy_len; memcpy((void*)queue[*p_head].buf, buf, copy_len); *p_head = next_head;
                    }
                }
            }
        }
    }
}
#endif
