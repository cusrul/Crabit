#ifndef APP_PRO_TOOLS_H
#define APP_PRO_TOOLS_H

#include "../src/NanoUI.h"
#include "wifi_hack_core.h"

// 引入全频段信道列表 (需要在 wifi_hack_sniffer.h 或 core.h 里 extern 出来)
extern const int CHANNELS_2GHZ[];
extern const int CHANNELS_5GHZ[];

// ==========================================
// 2.4G/5G 双频段流量频谱仪
// ==========================================
static volatile uint32_t spectrum_byte_count = 0;
extern "C" void spectrum_promisc_callback(unsigned char *buf, unsigned int len, void *userdata) {
    (void)userdata; (void)buf;
    spectrum_byte_count += len; // 用捕获的空中字节数代表信道拥挤度
}

class AppSpectrum : public Application {
private:
    int band_mode = 0; // 0 = 2.4GHz, 1 = 5GHz
    
    // 峰值保持与当前值数组
    int current_vals[25] = {0};
    float peak_vals[25] = {0};
    
    uint32_t scan_timer = 0;
    int ch_idx = 0;
    int max_activity_ch = 0;

    const int NUM_2G = 14;
    const int NUM_5G = 25; // 示例：常用的 5G 信道数量，需要确保 CHANNELS_5GHZ 有这么多

public:
    const char* getAppName() override { return "频谱"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; } 

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        // 安全状态重置：先全关，再进入混杂
        wifi_off(); safe_delay(100);
        wifi_on(RTW_MODE_PROMISC); safe_delay(100);
        wifi_enter_promisc_mode();
        wifi_set_promisc(RTW_PROMISC_ENABLE_2, spectrum_promisc_callback, 1);

        bool running = true;
        scan_timer = hal_get_ticks();
        
        while(running && is_display_active) {
            UIEvent e = input->poll();
            if (e.type == EVT_BACK) running = false;
            
            // 按键切换频段 (UP/DOWN/OK)
            if (e.type == EVT_DOWN || e.type == EVT_UP || e.type == EVT_OK) {
                band_mode = 1 - band_mode;
                ch_idx = 0;
                // 切换频段时清空数据，防止视觉污染
                memset(current_vals, 0, sizeof(current_vals));
                memset(peak_vals, 0, sizeof(peak_vals));
            }

            // 超高速跳频与流量捕获 (每个信道仅停留 20ms)
            if (hal_get_ticks() - scan_timer > 20) {
                int total_ch = (band_mode == 0) ? NUM_2G : NUM_5G;
                const int* ch_array = (band_mode == 0) ? CHANNELS_2GHZ : CHANNELS_5GHZ;

                // 1. 记录并归一化上一个信道的流量
                current_vals[ch_idx] = spectrum_byte_count;
                // 峰值保持逻辑
                if (current_vals[ch_idx] > peak_vals[ch_idx]) {
                    peak_vals[ch_idx] = current_vals[ch_idx];
                }

                // 2. 寻找最拥挤信道
                int max_v = 0;
                for (int i=0; i<total_ch; i++) {
                    if (current_vals[i] > max_v) { max_v = current_vals[i]; max_activity_ch = ch_array[i]; }
                }

                // 3. 切换到下一个信道并清空计数器
                ch_idx = (ch_idx + 1) % total_ch;
                wifi_set_channel(ch_array[ch_idx]);
                spectrum_byte_count = 0; 
                scan_timer = hal_get_ticks();
            }

            // 重力衰减算法 (Peak Decay)
            for(int i=0; i<25; i++) {
                if (peak_vals[i] > 0) peak_vals[i] *= 0.95; // 模拟重力，缓慢下降
            }

            // ================== UI 渲染 (专业瀑布流直方图) ==================
            canvas->clear();
            
            // 顶栏：赛博标题
            canvas->fillRect(0, 0, 128, 14, C_WHITE);
            String title = (band_mode == 0) ? "2.4吉赫兹频谱" : "5.8吉赫兹频谱";
            canvas->drawText(2, 1, title.c_str(), 8, true);
            
            // 背景刻度网格 (每 10 像素一条)
            for (int y = 17; y < 43; y += 8) {
                for(int x = 0; x < 128; x += 4) canvas->drawPixel(x, y, C_WHITE);
            }

            // 绘制直方图柱子与峰值点
            int total_ch = (band_mode == 0) ? NUM_2G : NUM_5G;
            int bar_w = 128 / total_ch; // 动态计算柱子宽度
            
            // 动态找到当前频段的最大峰值，用于 Y 轴归一化
            float global_max = 1.0;
            for(int i=0; i<total_ch; i++) { if(peak_vals[i] > global_max) global_max = peak_vals[i]; }
            
            for(int i=0; i<total_ch; i++) {
                int x = i * bar_w;
                // 计算柱子高度 (0 - 27px)，为可读底栏留出空间
                int bar_h = (int)((current_vals[i] / global_max) * 27.0);
                int peak_h = (int)((peak_vals[i] / global_max) * 27.0);
                
                // 画当前流量柱子 (fillRect 底部坐标是 y+h，所以要 52-h)
                canvas->fillRect(x + 1, 45 - bar_h, bar_w - 2, bar_h, C_WHITE);
                // 画峰值悬浮点
                canvas->drawLine(x + 1, 45 - peak_h, x + bar_w - 2, 45 - peak_h, C_WHITE);
            }

            // 底栏信息：战术监控点
            canvas->drawLine(0, 47, 128, 47, C_WHITE);
            canvas->drawText(2, 49, "按键切换频段", 8, false);
            char buf[32]; snprintf(buf, sizeof(buf), "PK:%d", max_activity_ch);
            canvas->drawText(96, 51, buf, 8, false);

            canvas->display();
            safe_delay(16); // 限制刷新率
        }
        
        // 彻底关闭混杂模式，释放硬件资源
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); wifi_off();
    }
};


// ==========================================
// MAC 雷达
// ==========================================
// 高级雷达数据结构：引入最后活跃时间和探针计数
struct RadarTarget {
    uint8_t mac[6];
    uint32_t first_seen;
    uint32_t last_seen;
    uint32_t hits;
    bool is_random; // 新增：识别是否为随机隐私 MAC
};

static RadarTarget radar_targets[32];
static int radar_target_count = 0;
static uint8_t locked_mac[6];
static volatile uint32_t locked_mac_last_seen = 0;
static volatile uint32_t locked_hits_recent = 0; // 锁定后最近 3 秒内的探针数
static volatile bool is_monitoring = false;

extern "C" void radar_promisc_callback(unsigned char *buf, unsigned int len, void *userdata) {
    (void)userdata;
    if (len < 24) return;
    
    // 只抓手机主动发出的探针请求包 (Probe Request: 0x40)
    if (buf[0] == 0x40) {
        if (is_monitoring) {
            if (memcmp(&buf[10], locked_mac, 6) == 0) {
                locked_mac_last_seen = hal_get_ticks();
                locked_hits_recent++; // 增加锁定后的活跃计数
            }
            return;
        }

        uint32_t now = hal_get_ticks();
        // 扫描列表去重与更新
        for (int i = 0; i < radar_target_count; i++) {
            if (memcmp(radar_targets[i].mac, &buf[10], 6) == 0) {
                radar_targets[i].last_seen = now;
                radar_targets[i].hits++;
                return; 
            }
        }
        // 新增目标进入缓冲区
        if (radar_target_count < 32) {
            memcpy(radar_targets[radar_target_count].mac, &buf[10], 6);
            radar_targets[radar_target_count].first_seen = now;
            radar_targets[radar_target_count].last_seen = now;
            radar_targets[radar_target_count].hits = 1;
            // 识别本地管理 MAC 位 (Local Admin Bit)，通常表示是随机地址 (0x02)
            radar_targets[radar_target_count].is_random = (buf[10] & 0x02) != 0;
            radar_target_count++;
        }
    }
}

class AppMacRadar : public Application {
private:
    enum State { STATE_SCAN, STATE_LIST, STATE_MONITOR };
    State state = STATE_SCAN;
    int list_idx = 0;
    int list_offset = 0;
    int scan_ch_idx = 0;
    uint32_t scan_timer = 0;

    // 智能数据老化清理：踢掉离开超过 60 秒的设备
    void cleanup_old_targets() {
        uint32_t now = hal_get_ticks();
        int valid_count = 0;
        for (int i = 0; i < radar_target_count; i++) {
            if (now - radar_targets[i].last_seen < 60000) { // 60 秒阈值
                radar_targets[valid_count] = radar_targets[i];
                valid_count++;
            }
        }
        radar_target_count = valid_count;
    }

public:
    const char* getAppName() override { return "设备雷达"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        // 安全初始化 WiFi 栈
        wifi_off(); safe_delay(100); 
        wifi_on(RTW_MODE_PROMISC); safe_delay(100);
        wifi_enter_promisc_mode();
        
        radar_target_count = 0;
        is_monitoring = false;
        // 挂载高级混杂模式回调
        wifi_set_promisc(RTW_PROMISC_ENABLE_2, radar_promisc_callback, 1);
        
        bool running = true;
        state = STATE_SCAN;
        scan_ch_idx = 0;
        scan_timer = hal_get_ticks();
        wifi_set_channel(CHANNELS_2GHZ[0]); // 老板一般连 2.4G

        char buf[32];

        while(running && is_display_active) {
            UIEvent e = input->poll();
            uint32_t now = hal_get_ticks();

            if (e.type == EVT_BACK) {
                if (state == STATE_MONITOR || state == STATE_LIST) {
                    state = STATE_SCAN; // 回到扫描界面，重新搜寻
                    is_monitoring = false;
                } else running = false;
            }

            // ================== 状态逻辑控制 ==================
            if (state == STATE_SCAN) {
                // 150ms 跳一次信道
                if (now - scan_timer > 150) {
                    scan_ch_idx = (scan_ch_idx + 1) % 14; 
                    wifi_set_channel(CHANNELS_2GHZ[scan_ch_idx]);
                    scan_timer = now;
                    // 扫描时在后台执行智能老化清理
                    cleanup_old_targets();
                }
                // 按 OK 进入选择列表
                if (e.type == EVT_OK) {
                    cleanup_old_targets(); // 进列表前最后扫一圈
                    if (radar_target_count > 0) { list_idx = 0; list_offset = 0; state = STATE_LIST; }
                }
            } 
            else if (state == STATE_LIST) {
                // 优化后的 3 项列表滚动逻辑
                if (e.type == EVT_DOWN) { 
                    if (list_idx < radar_target_count - 1) list_idx++;
                    if (list_idx >= list_offset + 3) list_offset = list_idx - 2;
                }
                if (e.type == EVT_UP) { 
                    if (list_idx > 0) list_idx--;
                    if (list_idx < list_offset) list_offset = list_idx;
                }
                // 按 OK 锁定目标
                if (e.type == EVT_OK) {
                    memcpy(locked_mac, radar_targets[list_idx].mac, 6);
                    is_monitoring = true;
                    locked_mac_last_seen = now; // 假设锁定瞬间看见它
                    locked_hits_recent = 0;
                    state = STATE_MONITOR;
                }
            }
            else if (state == STATE_MONITOR) {
                // 锁定模式下继续高速跳频巡航，防止目标切换了信道
                if (now - scan_timer > 150) {
                    scan_ch_idx = (scan_ch_idx + 1) % 14;
                    wifi_set_channel(CHANNELS_2GHZ[scan_ch_idx]);
                    scan_timer = now;
                }
                // 每隔3秒重置一次锁定后的“最近探针数”，用于计算威胁等级
                static uint32_t hit_reset_timer = 0;
                if (now - hit_reset_timer > 3000) {
                    locked_hits_recent = 0;
                    hit_reset_timer = now;
                }
            }

            // ================== UI Professional 渲染 ==================
            canvas->clear();
            if (state == STATE_SCAN) {
                // 赛博标题栏
                canvas->fillRect(0, 0, 128, 14, C_WHITE);
                canvas->drawText(2, 1, "设备扫描", 8, true);
                
                // 动态雷达扫描动画
                int base_r = (now / 20) % 30;
                canvas->drawCircle(64, 32, base_r, C_WHITE);
                canvas->drawCircle(64, 32, (base_r > 10) ? base_r - 10 : 0, C_WHITE); // 双重波纹
                
                // 居中显示已捕获 MAC 数量
                snprintf(buf, sizeof(buf), "设备: %d", radar_target_count);
                canvas->fillRect(43, 26, 42, 13, C_BLACK); // 用黑矩形盖住后面的雷达波紋
                const int16_t countWidth = get_utf8_string_width(buf, 8);
                canvas->drawText(43 + (42 - countWidth) / 2, 26, buf, 8);
                
                // 底栏：战术信息
                canvas->drawLine(0, 49, 128, 49, C_WHITE);
                canvas->drawText(2, 51, "确认键查看列表", 8, false);
            } 
            else if (state == STATE_LIST) {
                canvas->fillRect(0, 0, 128, 14, C_WHITE);
                canvas->drawText(2, 1, "地址 R随机 P固定", 8, true);

                // 高级 3 行列表：显示 MAC、探针Hits 和 Aging 时间
                for (int i = 0; i < 3; i++) {
                    int idx = list_offset + i;
                    if (idx >= radar_target_count) break;
                    
                    int y = 14 + (i * 12);
                    RadarTarget& t = radar_targets[idx];
                    
                    if (idx == list_idx) {
                        canvas->fillRect(0, y - 1, 128, 13, C_WHITE); // 高亮条
                    }
                    
                    // 紧凑 ASCII 地址，R/P 含义由标题说明
                    snprintf(buf, sizeof(buf), "%02X:%02X..%02X %c",
                        t.mac[0], t.mac[1], t.mac[5], t.is_random ? 'R' : 'P');
                    canvas->drawText(2, y + 2, buf, 8, idx == list_idx);
                    
                    // 右侧靠边显示老化时间 (s 前 seen)
                    int age_sec = (now - t.last_seen) / 1000;
                    snprintf(buf, sizeof(buf), "%ds", age_sec);
                    canvas->drawText(103, y + 2, buf, 8, idx == list_idx);
                }
                // 底部说明
                canvas->drawLine(0, 50, 128, 50, C_WHITE);
                canvas->drawText(2, 51, "返回 确认键锁定", 8, false);
            }
            else if (state == STATE_MONITOR) {
                int age_sec = (now - locked_mac_last_seen) / 1000;
                // 判定威胁等级：如果 10 秒内没发包，就不算“危险临近”
                bool is_danger = (age_sec < 10); 
                bool blink_phase = is_danger && ((now / 150) % 2 == 0);
                
                if (is_danger) {
                    // 高威胁时全屏反色闪烁
                    canvas->fillRect(0, 0, 128, 64, blink_phase ? C_WHITE : C_BLACK);
                    
                    // 顶部警报栏
                    canvas->fillRect(0, 0, 128, 14, blink_phase ? C_BLACK : C_WHITE);
                    canvas->drawText(2, 1, "设备接近提醒", 8, !blink_phase);
                    
                    // 绘制锁定十字准星动画
                    canvas->drawCircle(64, 32, 15, blink_phase ? C_BLACK : C_WHITE);
                    canvas->drawLine(64, 10, 64, 54, blink_phase ? C_BLACK : C_WHITE); // 垂直线
                    canvas->drawLine(40, 32, 88, 32, blink_phase ? C_BLACK : C_WHITE); // 水平线
                    
                    // 动态计算威胁百分比 (基于 3 秒内 Probe 密集度)
                    int threat_level = locked_hits_recent * 10; // 一个包代表 10% 威胁
                    if (threat_level > 100) threat_level = 100;
                    
                    snprintf(buf, sizeof(buf), "接近程度: %d%%", threat_level);
                    // 用反色小块显示威胁值，使其不被十字线盖住
                    canvas->fillRect(8, 25, 112, 14, blink_phase ? C_BLACK : C_WHITE);
                    canvas->drawText(10, 26, buf, 8, !blink_phase);

                } else {
                    // 安全监控模式 (Searching)
                    canvas->fillRect(0, 0, 128, 14, C_WHITE);
                    canvas->drawText(2, 1, "设备已锁定", 8, true);
                    
                    snprintf(buf, sizeof(buf), "%d秒前出现", age_sec);
                    const int16_t ageWidth = get_utf8_string_width(buf, 8);
                    canvas->drawText((128 - ageWidth) / 2, 22, buf, 8);
                    // 居中显示雷达动画
                    canvas->drawCircle(64, 38, (now / 10) % 30, C_WHITE);
                    const char* searching = "正在搜索…";
                    const int16_t searchingWidth = get_utf8_string_width(searching, 8);
                    canvas->drawText((128 - searchingWidth) / 2, 33, searching, 8);
                }
                
                // 底部：始终显示目标硬件 MAC
                canvas->drawLine(0, 54, 128, 54, is_danger && blink_phase ? C_BLACK : C_WHITE);
                snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X", 
                    locked_mac[0], locked_mac[1], locked_mac[2], locked_mac[3], locked_mac[4], locked_mac[5]);
                canvas->drawText(16, 56, buf, 8, is_danger && blink_phase);
            }

            canvas->display();
            // 主循环底部休息 16ms，限制 60FPS
            safe_delay(16);
        }
        
        // 退出前必须彻底清理混杂模式，否则下个 APP 初始化 WiFi 时会卡死内核
        wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0); 
        wifi_off();
    }
};

#endif
