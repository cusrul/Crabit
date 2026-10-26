#ifndef GLOBAL_STORE_H
#define GLOBAL_STORE_H

#include <stdint.h>
#include <string.h>

// 跨任务共享的状态与业务数据。
struct GlobalDataStore {
    // 状态类
    bool wifi_connected = false;
    int  ble_client_count = 0;
    
    // 业务类
    char current_word[32] = {0};     // 背单词
    char latest_weather[32] = {0};   // 天气
    char latest_notify[64] = {0};    // 通知

    // 专门的状态位
    char ble_status[16] = {0};       // "DISCONN", "CONN", "SCANNING"
    char wifi_status[16] = {0};      // "OFF", "IDLE", "CONNECTED"
    char net_time[16] = {0};         // "12:00:00"
    int  rssi = 0;                   // 信号强度

    // ==========================================
    // 系统全局偏好，可打包写入 Flash
    // ==========================================
    uint8_t sys_volume = 100;        // 默认音量 100%
    uint8_t active_watch_face = 0;   // 当前表盘 (0:标准, 1:二进制)

    // ==========================================
    // 发声请求，由音频驱动消费
    // ==========================================
    uint16_t req_buzzer_freq = 0;     // 请求的频率 (Hz)
    uint16_t req_buzzer_duration = 0; // 请求的时长 (ms)
};

extern GlobalDataStore GlobalStore;

// 音频请求入口：写入 GlobalStore，由驱动在更新周期内发声。
class SysAudio {
public:
    static void play(uint16_t freq, uint16_t duration_ms) {
        GlobalStore.req_buzzer_freq = freq;
        GlobalStore.req_buzzer_duration = duration_ms;
    }
};

#endif