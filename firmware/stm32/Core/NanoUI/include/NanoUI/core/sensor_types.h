#ifndef NANOUI_SENSOR_TYPES_H
#define NANOUI_SENSOR_TYPES_H

#include <stdint.h>

namespace NanoUI {

// ====================================================================
// 核心基类：SensorData (所有传感器数据的“老祖宗”)
// 统一定义公共属性，方便后续进行数据老化检测或状态排查
// ====================================================================
struct SensorData {
    uint32_t timestamp;  // 数据最后更新的系统时间 (防止拿到过期死数据)
    bool is_valid;       // 传感器状态：是否在线/数据是否可信
};

// 1. QMI8658 惯性传感器
struct SensorDataIMU : public SensorData {
    float ax, ay, az;
    float gx, gy, gz; 
    uint32_t int1_count; // QMI INT1 触发次数 (PB1)
    uint32_t int2_count; // QMI INT2 触发次数 (PB0)
};

// 2. PAT9125 光学传感器
struct SensorDataOptical : public SensorData {
    int32_t x, y;
    int8_t dx, dy;
    uint8_t id1, id2;
    uint8_t mot, mode;
    float agc;
    bool lifted;
    uint32_t int_count;  // PAT 中断触发次数 (PA8)
};

// 3. 电池 ADC 传感器
struct SensorDataBattery : public SensorData {
    float voltage;
    int percent;
    bool is_charging;
};

// 3b. AXP2101 PMU 复合数据
struct SensorDataAXP2101 : public SensorData {
    float voltage;
    int percent;
    int current;
    bool is_charging;
    bool is_vbus_in;
    float temp;
    float v_dcdc1;
    float v_dcdc2;
    float v_dcdc3;
    float v_aldo1;
    float v_aldo2;
    float v_aldo3;
};

// 4. BMP280 气压计
struct SensorDataPress : public SensorData {
    float pressure;
    float temperature;
};

// 5. MMC5603NJ 地磁计
struct SensorDataMag : public SensorData {
    float x, y, z;
};

// ====================================================================
// 模板：以后你要添加任何新传感器（比如心率、温湿度），只需这样写：
// ====================================================================
/*
struct SensorDataHeartRate : public SensorData {
    uint8_t bpm;         // 心率值
    uint8_t spo2;        // 血氧饱和度
};
*/

} 
#endif
