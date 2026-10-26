#include "NanoUI/core/data_hub.h"
#include <stdint.h>
#include <string.h>

// 初始化全局数据枢纽 (公共账本)
SystemData DataHub::shared = []() {
    SystemData data;
    memset(&data, 0, sizeof(SystemData));
    
    // 设置一些默认基准值
    data.battery.percent = 100;
    data.battery.voltage = 4.2f;
    data.battery.is_valid = true;
    
    data.temperature = 25.0f;
    data.humidity = 50.0f;
    data.pressure = 1013.25f;
    
    data.totalHeap = 65536;
    data.freeHeap = 65536;
    
    return data;
}();
