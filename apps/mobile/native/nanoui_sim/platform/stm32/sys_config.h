#ifndef CRABIT_NANOUI_SIM_SYS_CONFIG_H
#define CRABIT_NANOUI_SIM_SYS_CONFIG_H

#include <stdint.h>
#include <cmath>
#include "watch_face.h"
#include "product_preferences.h"

struct SystemConfig {
    uint32_t screen_off_ms = 3000;
    uint32_t deep_sleep_ms = 10000;
    bool enable_lift_wake = true;
    bool enable_tap_wake = true;
    float wake_x_max = 0.3f, wake_y_min = -1.0f, wake_y_max = 1.0f, wake_z_min = 0.3f;
    float brightness = 0.5f;
    bool invert_disp = false;
    uint8_t watch_face = 0;
};
inline SystemConfig sys_cfg{};

class ConfigManager {
public:
    enum class SaveResult { Saved, UnlockFailed, EraseFailed, ProgramFailed, LockFailed };
    static SaveResult save() { return SaveResult::Saved; }
};

#endif
