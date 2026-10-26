#pragma once
#include "../../../firmware/stm32/Core/Inc/sensor_snapshot.h"
inline StmSensorSnapshot fakeSensor;
inline bool fakeSensorAvailable = true;
inline bool fakeSensorAutoUpdate = true;
inline bool STM32_GetUiSensorSnapshot(StmSensorSnapshot& out) {
    out = fakeSensor;
    return fakeSensorAvailable;
}
