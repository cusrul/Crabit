#pragma once

#include "sensor_snapshot.h"

// Sensor sampling service. The I2C bus itself is owned by I2cBus.
void Task_SensorHub(void* argument);
