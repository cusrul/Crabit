#ifndef APP_AXP2101_H
#define APP_AXP2101_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include "sensor_ui.h"
#include <cstdio>
#include <vector>
#include "stm_sensor_service.h"

class AXP2101App : public Application {
private:
    char str_batt[32];
    char str_power[24];
    char str_temp[24];
    char str_dc3[24];

public:
    AXP2101App() {
        snprintf(str_batt, sizeof(str_batt), "N/A");
        snprintf(str_power, sizeof(str_power), "N/A");
        snprintf(str_temp, sizeof(str_temp), "N/A");
        snprintf(str_dc3, sizeof(str_dc3), "N/A");
    }

    const char* getAppName() override { return "AXP2101"; }

    void onTick() override {
        StmSensorSnapshot snapshot{};
        if (!STM32_GetUiSensorSnapshot(snapshot)) return;
        const auto* data = &snapshot.power;
        if (!data->is_valid) {
            SetSensorText(str_batt, sizeof(str_batt), "N/A");
            SetSensorText(str_power, sizeof(str_power), "N/A");
            SetSensorText(str_temp, sizeof(str_temp), "N/A");
            SetSensorText(str_dc3, sizeof(str_dc3), "N/A");
            return;
        }

        char buf[32];
        if (data->voltage > 0.5f) {
            char vbuf[16];
            FormatFixed2(vbuf, sizeof(vbuf), data->voltage, "V");
            if (data->percent > 0) snprintf(buf, sizeof(buf), "%s %d%%", vbuf, data->percent);
            else snprintf(buf, sizeof(buf), "%s", vbuf);
            SetSensorText(str_batt, sizeof(str_batt), buf);
        } else {
            SetSensorText(str_batt, sizeof(str_batt), "N/A");
        }

        const char* power_state = "BAT";
        if (data->is_charging && data->is_vbus_in) power_state = "CHG/VBUS";
        else if (data->is_charging) power_state = "CHG";
        else if (data->is_vbus_in) power_state = "VBUS";
        SetSensorText(str_power, sizeof(str_power), power_state);

        if (data->temp >= -20.0f && data->temp <= 120.0f) {
            FormatFixed1(buf, sizeof(buf), data->temp, "C");
            SetSensorText(str_temp, sizeof(str_temp), buf);
        } else {
            SetSensorText(str_temp, sizeof(str_temp), "N/A");
        }

        if (data->v_dcdc3 > 0.5f) {
            FormatFixed1(buf, sizeof(buf), data->v_dcdc3, "V");
            SetSensorText(str_dc3, sizeof(str_dc3), buf);
        } else {
            SetSensorText(str_dc3, sizeof(str_dc3), "N/A");
        }
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> rows;

        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorTitle("电源管理", "AXP2101"));
        rows.push_back(new SizedBox(0, 10));
        rows.push_back(BuildSensorMenuRow("电池电量", str_batt));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("供电状态", str_power));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("芯片温度", str_temp));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("DCDC3", str_dc3));
        rows.push_back(new SizedBox(0, 16));
        rows.push_back(BuildSensorBackRow());
        rows.push_back(new SizedBox(0, 6));

        return new PageTransition(
            new Container(new ScrollColumn(rows), 128, 64, 0, false, 0),
            TransitionType::SlideFromRight
        );
    }
};

#endif
