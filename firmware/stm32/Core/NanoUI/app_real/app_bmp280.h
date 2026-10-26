#ifndef APP_BMP280_H
#define APP_BMP280_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include "sensor_ui.h"
#include "stm_sensor_service.h"
#include <cstdio>
#include <vector>

class BMP280App : public Application {
   private:
    char str_pressure[24];
    char str_temp[24];
    char str_state[24];

   public:
    BMP280App() {
        snprintf(str_pressure, sizeof(str_pressure), "N/A");
        snprintf(str_temp, sizeof(str_temp), "N/A");
        snprintf(str_state, sizeof(str_state), "N/A");
    }

    const char* getAppName() override { return "BMP280"; }

    void onTick() override {
        StmSensorSnapshot snapshot{};
        if (!STM32_GetUiSensorSnapshot(snapshot))
            return;
        const auto* data = &snapshot.pressure;
        if (!data || !data->is_valid) {
            SetSensorText(str_pressure, sizeof(str_pressure), "N/A");
            SetSensorText(str_temp, sizeof(str_temp), "N/A");
            SetSensorText(str_state, sizeof(str_state), "OFFLINE");
            return;
        }

        char buf[24];
        FormatFixed1(buf, sizeof(buf), data->pressure, "hPa");
        SetSensorText(str_pressure, sizeof(str_pressure), buf);
        FormatFixed1(buf, sizeof(buf), data->temperature, "C");
        SetSensorText(str_temp, sizeof(str_temp), buf);
        SetSensorText(str_state, sizeof(str_state), "OK");
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> rows;

        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorTitle("环境气压", "BMP280"));
        rows.push_back(new SizedBox(0, 10));
        rows.push_back(BuildSensorMenuRow("当前气压", str_pressure));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("当前温度", str_temp));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("传感器", str_state));
        rows.push_back(new SizedBox(0, 16));
        rows.push_back(BuildSensorBackRow());
        rows.push_back(new SizedBox(0, 6));

        return new Container(new ScrollColumn(rows), 128, 64, 0, false, 0);
    }
};

#endif
