#ifndef SYS_CONFIG_H
#define SYS_CONFIG_H

#include <stdint.h>

struct SystemConfig {
    // 1. 息屏与休眠时间（毫秒）；设为 0 表示不自动触发。
    uint32_t screen_off_ms = 1000000; 
    uint32_t kill_app_ms   = 1500000; 
    uint32_t deep_sleep_ms = 3000000; 
    
    // 2. 唤醒逻辑配置
    bool  enable_lift_wake = true; // 抬腕唤醒
    bool  enable_tap_wake  = true; // 敲击震动唤醒（QMI Any-Motion）
    
    // 3. 抬腕姿态 3D 阈值 (1g = 1.0f)
    float wake_x_max = 0.3f;  // X轴限制 (防手臂垂直下垂)
    float wake_y_min = -1.0f;  // Y 轴下限：必须翻腕
    float wake_y_max = 1.0f;  // Y 轴上限：防止手腕过度翻转背对眼睛
    float wake_z_min = 0.3f;  // Z轴必须朝上

    // 4. 屏幕配置
    float brightness = 0.5f;   
    bool  invert_disp = false; 
};

extern SystemConfig sys_cfg;

#endif