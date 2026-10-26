#ifndef CRABIT_NANOUI_SIM_SENSOR_SERVICE_H
#define CRABIT_NANOUI_SIM_SENSOR_SERVICE_H

#include <stdint.h>
#include "NanoUI/core/sensor_types.h"

struct StmSensorSnapshot {
    uint32_t published_at = 0;
    NanoUI::SensorDataIMU imu{};
    NanoUI::SensorDataOptical optical{};
    NanoUI::SensorDataPress pressure{};
    NanoUI::SensorDataBattery battery{};
    NanoUI::SensorDataAXP2101 power{};
};

inline bool STM32_GetUiSensorSnapshot(StmSensorSnapshot& out) {
    const uint32_t now = hal_get_ticks();
    out.published_at = now;
    out.imu = {};
    out.imu.is_valid = true;
    out.imu.ax = 0.02f;
    out.imu.ay = -0.06f;
    out.imu.az = 0.98f;
    out.imu.timestamp = now;
    out.optical = {};
    out.optical.is_valid = true;
    out.optical.dx = 0;
    out.optical.dy = 0;
    out.optical.agc = 42;
    out.optical.mode = 3;
    out.optical.lifted = true;
    out.optical.timestamp = now;
    out.pressure = {};
    out.pressure.is_valid = true;
    out.pressure.pressure = 1013.25f;
    out.pressure.temperature = 24.6f;
    out.pressure.timestamp = now;
    out.battery = {};
    out.battery.is_valid = true;
    out.battery.voltage = 3.91f;
    out.battery.percent = 78;
    out.battery.is_charging = false;
    out.battery.timestamp = now;
    out.power = {};
    out.power.is_valid = true;
    out.power.voltage = 3.91f;
    out.power.percent = 78;
    out.power.temp = 29.4f;
    out.power.v_dcdc3 = 3.10f;
    out.power.is_charging = false;
    out.power.is_vbus_in = false;
    out.power.timestamp = now;
    return true;
}
inline bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out) {
    return out && STM32_GetUiSensorSnapshot(*out);
}

#endif
