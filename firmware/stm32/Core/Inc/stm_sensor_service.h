#ifndef CRABIT_STM_SENSOR_SERVICE_H
#define CRABIT_STM_SENSOR_SERVICE_H

#include "sensor_snapshot.h"

// UI-facing read helper. It copies a coherent snapshot and never exposes a
// Sensor* or an I2C handle to a page/widget.
inline bool STM32_GetUiSensorSnapshot(StmSensorSnapshot& snapshot) {
    return STM32_ReadSensorSnapshot(&snapshot);
}

#endif
