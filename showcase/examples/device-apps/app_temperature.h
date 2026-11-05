#pragma once

#include <NanoUI.h>
#include "stm_sensor_service.h"
#include "sensor_ui.h"
#include <cmath>
#include <cstring>

class TemperatureApp : public Application {
    char value[24] = "N/A";

public:
    const char* getAppName() override { return "Temperature"; }
    const char* displayedValue() const { return value; }

    void onTick() override {
        StmSensorSnapshot snapshot{};
        char next[24] = "N/A";
        if (STM32_GetUiSensorSnapshot(snapshot) && snapshot.pressure.is_valid &&
            uint32_t(hal_get_ticks() - snapshot.pressure.timestamp) <= 5000 &&
            std::isfinite(snapshot.pressure.temperature) &&
            snapshot.pressure.temperature >= -100.0f && snapshot.pressure.temperature <= 150.0f) {
            FormatFixed1(next, sizeof(next), snapshot.pressure.temperature, " C");
        }
        if (std::strcmp(value, next) != 0) {
            std::strncpy(value, next, sizeof(value) - 1);
            value[sizeof(value) - 1] = '\0';
            if (WindowManager::instance)
                WindowManager::instance->markNeedsLayout();
        }
    }

    Widget* buildUI() override {
        return new Container(new Column({
            new Text("BMP280 temperature", 8),
            new SizedBox(0, 8),
            new Text(value, 8),
            new SizedBox(0, 8),
            new Text("BACK to return", 8)
        }), 128, 64);
    }
};
