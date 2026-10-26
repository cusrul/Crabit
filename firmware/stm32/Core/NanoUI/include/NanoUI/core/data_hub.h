#ifndef CORE_DATA_HUB_H
#define CORE_DATA_HUB_H

#include <stdint.h>

#include "sensor_types.h"

// =========================================================
// DataHub: 硬件与 UI 的隔离层 (公共账本)
// =========================================================
struct SystemData {
    // 系统基础状态
    uint32_t uptimeMs;
    uint32_t freeHeap;
    uint32_t totalHeap;
    uint32_t peakUsedHeap;

    // 核心传感器“黑板”
    NanoUI::SensorDataBattery battery;
    NanoUI::SensorDataIMU     imu;
    NanoUI::SensorDataOptical optical;
    NanoUI::SensorDataPress   env;
    NanoUI::SensorDataMag     mag;

    // 兼容性字段
    float temperature;
    float humidity;
    float pressure;
};

class DataHub {
public:
    static SystemData shared; 
};

#endif