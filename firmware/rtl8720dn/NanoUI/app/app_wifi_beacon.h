#ifndef APP_WIFI_BEACON_H
#define APP_WIFI_BEACON_H

#include "../src/NanoUI.h"
#include "wifi_hack_core.h"
#include "wifi_hack_sniffer.h"
#include "../packet-injection.h"

struct FakeAP {
    uint8_t mac[6];
    char ssid[33];
    int channel;
};

#define MAX_SPAM_COUNT 60
static FakeAP global_spam_list[MAX_SPAM_COUNT];

class AppWifiBeacon : public Application {
private:
    enum State { STATE_MODE_SEL, STATE_LIVE_RADAR, STATE_TACTIC, STATE_ATTACK };
    State state = STATE_MODE_SEL;

    int scan_mode = 1; // 0 = 被动, 1 = 主动
    int radarIndex = 0;  
    int radarOffset = 0;
    int tacticIndex = 0;
    int list_idx = 0;
    
    int scan_ch_idx = 0;
    uint32_t scan_timer = 0;
    uint32_t anim_frame = 0;
    int spam_index = 0;

    const char* tacticItems[3] = {
        "1. 随机名称",
        "2. 预设名称",
        "3. 名称克隆"
    };

    void inject_broadcast_probe() {
        uint8_t probe_frame[38] = {
            0x40, 0x00, 0x00, 0x00,             
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
            0x02, 0x11, 0x22, 0x33, 0x44, 0x55, 
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
            0x00, 0x00,                         
            0x00, 0x00,                         
            0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c 
        };
        wifi_tx_raw_frame((void*)probe_frame, 38);
    }

    void inject_beacon(uint8_t* bssid, const char* ssid, int channel) {
        uint8_t frame[128] = {
            0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
            0x64, 0x00, 0x31, 0x04  
        };
        memcpy(&frame[10], bssid, 6);
        memcpy(&frame[16], bssid, 6);
        int len = 36;
        frame[len++] = 0x00; 
        int slen = strlen(ssid);
        if (slen > 32) slen = 32;
        frame[len++] = (uint8_t)slen;
        memcpy(&frame[len], ssid, slen);
        len += slen;
        
        if (channel <= 14) {
            uint8_t rates_2g[] = {0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
            memcpy(&frame[len], rates_2g, 10); len += 10;
            frame[len++] = 0x03; frame[len++] = 0x01; frame[len++] = (uint8_t)channel;
        } else {
            uint8_t rates_5g[] = {0x01, 0x08, 0x8c, 0x12, 0x98, 0x24, 0xb0, 0x48, 0x60, 0x6c};
            memcpy(&frame[len], rates_5g, 10); len += 10;
        }
        
        wifi_tx_raw_frame((void*)frame, (size_t)len);
    }

    void drawArsenalItem(Canvas* canvas, int y, const char* text, bool isSelected) {
        if (isSelected) {
            canvas->fillRect(0, y, 124, 12, C_WHITE);
            canvas->drawText(2, y, text, 8, true);
        } else {
            canvas->drawText(2, y, text, 8, false);
        }
    }

    void build_spam_list(int mode, WiFiResult* target = NULL) {
        for (int i = 0; i < MAX_SPAM_COUNT; i++) {
            global_spam_list[i].mac[0] = 0x02;
            for(int j=1; j<6; j++) global_spam_list[i].mac[j] = rand() % 256;

            if (mode == 0) {
                snprintf(global_spam_list[i].ssid, 32, "Free_WiFi_%02X%d", (uint8_t)rand(), i);
                global_spam_list[i].channel = (i % 13) + 1;
            } 
            else if (mode == 1) {
                const char* j[] = {"[SYS_ERR]", "FBI_VAN", "System_Fail", "Searching"};
                snprintf(global_spam_list[i].ssid, 32, "%s_%02d", j[i % 4], i);
                global_spam_list[i].channel = target ? target->channel : 6;
            }
            else if (mode == 2 && target) { 
                String final_name = "";
                int pre = i % 8; int post = (i / 8) % 8;
                for(int s=0; s<pre; s++) final_name += " ";
                final_name += target->ssid;
                for(int s=0; s<post; s++) final_name += " ";
                strncpy(global_spam_list[i].ssid, final_name.c_str(), 32);
                global_spam_list[i].ssid[32] = '\0';
                global_spam_list[i].channel = target->channel;
            }
        }
    }

public:
    const char* getAppName() override { return "信标测试"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        if (!g_hackCtx) g_hackCtx = new WifiHackContext();
        
        bool running = true;
        state = STATE_MODE_SEL;
        scan_mode = 1;
        char buf[32];

        while (running && is_display_active) {
            UIEvent e = input->poll();
            anim_frame++;

            // ================== 逻辑与状态跳转 ==================
            if (e.type == EVT_BACK) {
                if (state == STATE_ATTACK || state == STATE_TACTIC) {
                    state = STATE_LIVE_RADAR;
                } else if (state == STATE_LIVE_RADAR) {
                    wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); wifi_off();
                    state = STATE_MODE_SEL;
                } else {
                    running = false;
                }
            }

            if (state == STATE_MODE_SEL) {
                if (e.type == EVT_DOWN || e.type == EVT_UP) scan_mode = 1 - scan_mode;
                if (e.type == EVT_OK) {
                    wifi_off(); safe_delay(50); wifi_on(RTW_MODE_PROMISC); safe_delay(50);
                    wifi_enter_promisc_mode();
                    g_hackCtx->reset();
                    g_hackCtx->currentMode = SNIFF_ALL;
                    wifi_set_promisc(RTW_PROMISC_ENABLE_2, master_promisc_callback, 1);
                    scan_ch_idx = 0;
                    wifi_set_channel(DEEP_SCAN_CHANNELS[0]);
                    scan_timer = hal_get_ticks();
                    radarIndex = 0; radarOffset = 0;
                    state = STATE_LIVE_RADAR;
                }
            }
            else if (state == STATE_LIVE_RADAR) {
                if (hal_get_ticks() - scan_timer > 200) {
                    scan_ch_idx = (scan_ch_idx + 1) % NUM_DEEP_CHANNELS;
                    wifi_set_channel(DEEP_SCAN_CHANNELS[scan_ch_idx]);
                    if (scan_mode == 1) {
                        safe_delay(5);
                        inject_broadcast_probe();
                    }
                    scan_timer = hal_get_ticks();
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
                    if (radarIndex == 0) { build_spam_list(0); tacticIndex = 0; state = STATE_ATTACK; }
                    else { list_idx = radarIndex - 1; tacticIndex = 0; state = STATE_TACTIC; }
                }
            }
            else if (state == STATE_TACTIC) {
                if (e.type == EVT_DOWN && tacticIndex < 2) tacticIndex++;
                if (e.type == EVT_UP && tacticIndex > 0) tacticIndex--;
                if (e.type == EVT_OK) {
                    build_spam_list(tacticIndex == 2 ? 2 : 1, &g_hackCtx->scanResults[list_idx]);
                    wifi_set_channel(g_hackCtx->scanResults[list_idx].channel);
                    state = STATE_ATTACK;
                }
            }

            // ================== UI 渲染 ==================
            canvas->clear();
            
            if (state == STATE_MODE_SEL) {
                canvas->fillRect(0, 0, 128, 14, C_WHITE);
                canvas->drawText(2, 1, "扫描方式", 8, true);
                
                drawArsenalItem(canvas, 18, "被动监听", scan_mode == 0);
                drawArsenalItem(canvas, 34, "主动扫描", scan_mode == 1);
                
                canvas->drawLine(0, 50, 127, 50, C_WHITE);
                canvas->drawText(2, 52, "确认键开始扫描", 8, false);
            }
            else if (state == STATE_LIVE_RADAR) {
                canvas->fillRect(0, 0, 128, 14, C_WHITE);
                canvas->drawText(2, 1, "扫描", 8, true);
                snprintf(buf, 32, "CH%02d NET%02d", DEEP_SCAN_CHANNELS[scan_ch_idx], (int)g_hackCtx->scanResults.size());
                canvas->drawText(42, 2, buf, 8, true);

                for (int i = 0; i < 3; i++) {
                    int idx = radarOffset + i;
                    if ((size_t)idx > g_hackCtx->scanResults.size()) break;
                    int y_pos = 15 + (i * 12);
                    
                    if (idx == 0) {
                        drawArsenalItem(canvas, y_pos, "随机名称", radarIndex == 0);
                    } else {
                        WiFiResult &res = g_hackCtx->scanResults[idx - 1];
                        if (radarIndex == idx) canvas->fillRect(0, y_pos, 124, 12, C_WHITE);
                        Rect oldClip = canvas->getClipRect();
                        canvas->intersectClipRect({2, (int16_t)y_pos, 93, (int16_t)(y_pos + 12)});
                        canvas->drawText(2, y_pos, res.ssid.c_str(), 8, radarIndex == idx);
                        canvas->setClipRect(oldClip);
                        snprintf(buf, 32, "CH%02d", res.channel);
                        canvas->drawText(96, y_pos + 2, buf, 8, radarIndex == idx);
                    }
                }

                if (g_hackCtx->scanResults.size() > 2) {
                    // 使用三元表达式，避免依赖平台的 max 宏。
                    int calc_h = 36 / ((int)g_hackCtx->scanResults.size() + 1);
                    int barH = (calc_h > 4) ? calc_h : 4;
                    int barY = 15 + (radarIndex * (36 - barH)) / g_hackCtx->scanResults.size();
                    
                    canvas->drawLine(126, 15, 126, 50, C_WHITE);
                    canvas->fillRect(125, barY, 3, barH, C_WHITE);
                }

                canvas->drawLine(0, 51, 127, 51, C_WHITE);
                if (scan_mode == 1) canvas->drawText(2, 52, "主动扫描中", 8, false);
                else canvas->drawText(2, 52, "被动监听中", 8, false);
            }
            else if (state == STATE_TACTIC) {
                canvas->fillRect(0, 0, 128, 14, C_WHITE);
                canvas->drawText(2, 1, "选择测试方式", 8, true);
                
                const char* tacticLabels[3] = {"随机名称", "预设名称", "名称克隆"};
                for(int i=0; i<3; i++) {
                    drawArsenalItem(canvas, 15 + (i * 12), tacticLabels[i], tacticIndex == i);
                }
                
                canvas->drawLine(0, 51, 127, 51, C_WHITE);
                canvas->drawText(2, 52, "返回扫描列表", 8, false);
            }
            else if (state == STATE_ATTACK) {
                canvas->fillRect(0, 0, 128, 14, C_WHITE);
                String title = String(tacticItems[tacticIndex]).substring(3) + " [运行]";
                canvas->drawText(2, 1, title.c_str(), 8, true);
                
                int center_y = 30;
                for(int x = 2; x < 126; x += 4) {
                    int h = (rand() % 16) + 2; 
                    canvas->drawLine(x, center_y - h/2, x, center_y + h/2, C_WHITE);
                }
                
                canvas->drawText(2, 38, "广播名称", 8, false);
                snprintf(buf, 32, "TX:%lu", anim_frame * 5);
                canvas->drawText(88, 40, buf, 8, false);
                canvas->drawLine(0, 50, 127, 50, C_WHITE);
                canvas->drawTextInRect(2, 52, 124, 12, global_spam_list[spam_index].ssid, 0, 8);
            }
            
            canvas->display(); 

            // ================== 发包核心 ==================
            if (state == STATE_ATTACK) {
                for (int i = 0; i < 5; i++) {
                    FakeAP& ap = global_spam_list[spam_index];
                    if (tacticIndex == 0 && anim_frame % 10 == 0) wifi_set_channel(ap.channel);
                    inject_beacon(ap.mac, ap.ssid, ap.channel);
                    spam_index = (spam_index + 1) % MAX_SPAM_COUNT;
                }
                yield(); 
                safe_delay(2);
            } else {
                yield();
                safe_delay(10);
            }
        }
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); wifi_off();
    }
};
#endif
