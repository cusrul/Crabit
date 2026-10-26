#ifndef CRABIT_NANOUI_SIM_MAIN_H
#define CRABIT_NANOUI_SIM_MAIN_H

#include <stdint.h>
#include "NanoUI/hal/hal_time.h"

using HAL_StatusTypeDef = int;
constexpr HAL_StatusTypeDef HAL_OK = 0;
inline uint32_t HAL_GetTick() { return hal_get_ticks(); }

#endif
