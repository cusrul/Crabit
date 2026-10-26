#pragma once

#include "../../../firmware/shared/watch_face.h"

// Desktop service boundary only; product SettingsApp/ClockApp remain unchanged.
struct PreviewConfig {
    float brightness = 0.5f;
    bool invert_disp = false;
    uint32_t screen_off_ms = 10000, deep_sleep_ms = 30000;
    uint8_t watch_face = 0;
};
inline PreviewConfig sys_cfg;
class ConfigManager {
   public:
    enum class SaveResult { Saved, ProgramFailed };
    static SaveResult save();
};
