#ifndef CRABIT_STM32_SENSOR_SNAPSHOT_H
#define CRABIT_STM32_SENSOR_SNAPSHOT_H

#include <stdint.h>
#include "NanoUI/core/sensor_types.h"
#include "../../../shared/snapshot_store.h"

struct StmSensorSnapshot {
    uint32_t published_at = 0;
    NanoUI::SensorDataIMU imu{};
    NanoUI::SensorDataOptical optical{};
    NanoUI::SensorDataPress pressure{};
    NanoUI::SensorDataBattery battery{};
    NanoUI::SensorDataAXP2101 power{};
};

extern Crabit::SnapshotStore<StmSensorSnapshot> g_stm_sensor_snapshot;

// Copies the latest complete snapshot. It never touches I2C or a driver.
bool STM32_ReadSensorSnapshot(StmSensorSnapshot* out);

#endif
