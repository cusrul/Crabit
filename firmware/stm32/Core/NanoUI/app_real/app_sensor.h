#ifndef APP_SENSOR_H
#define APP_SENSOR_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include "sensor_ui.h"
#include "sys_config.h"
#include "stm_sensor_service.h"
#include <cmath>
#include <cstdio>
#include <vector>

extern SystemConfig sys_cfg;

class SensorApp : public Application {
   private:
    char str_ax[24];
    char str_ay[24];
    char str_az[24];
    char str_move[24];
    char str_wake[24];

   public:
    SensorApp() {
        snprintf(str_ax, sizeof(str_ax), "N/A");
        snprintf(str_ay, sizeof(str_ay), "N/A");
        snprintf(str_az, sizeof(str_az), "N/A");
        snprintf(str_move, sizeof(str_move), "N/A");
        snprintf(str_wake, sizeof(str_wake), "N/A");
    }

    const char* getAppName() override { return "QMI8658"; }

    void onTick() override {
        StmSensorSnapshot snapshot{};
        if (!STM32_GetUiSensorSnapshot(snapshot))
            return;
        const auto* data = &snapshot.imu;
        if (!data->is_valid) {
            SetSensorText(str_ax, sizeof(str_ax), "N/A");
            SetSensorText(str_ay, sizeof(str_ay), "N/A");
            SetSensorText(str_az, sizeof(str_az), "N/A");
            SetSensorText(str_move, sizeof(str_move), "OFFLINE");
            SetSensorText(str_wake, sizeof(str_wake), "N/A");
            return;
        }

        char buf[24];
        FormatFixed2(buf, sizeof(buf), data->ax, "g");
        SetSensorText(str_ax, sizeof(str_ax), buf);
        FormatFixed2(buf, sizeof(buf), data->ay, "g");
        SetSensorText(str_ay, sizeof(str_ay), buf);
        FormatFixed2(buf, sizeof(buf), data->az, "g");
        SetSensorText(str_az, sizeof(str_az), buf);

        float motion = std::fabs(data->ax) + std::fabs(data->ay) + std::fabs(data->az);
        SetSensorText(str_move, sizeof(str_move), motion > 1.8f ? "MOVE" : "STABLE");

        bool can_wake = sys_cfg.enable_lift_wake && std::fabs(data->ax) < sys_cfg.wake_x_max &&
                        std::fabs(data->ay) > sys_cfg.wake_y_min && data->az > sys_cfg.wake_z_min;
        SetSensorText(str_wake, sizeof(str_wake), can_wake ? "READY" : "NO");
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> rows;

        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorTitle("六轴姿态", "QMI8658"));
        rows.push_back(new SizedBox(0, 10));
        rows.push_back(BuildSensorMenuRow("X 轴", str_ax));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("Y 轴", str_ay));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("Z 轴", str_az));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("运动状态", str_move));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("抬腕唤醒", str_wake));
        rows.push_back(new SizedBox(0, 16));
        rows.push_back(BuildSensorBackRow());
        rows.push_back(new SizedBox(0, 6));

        return new Container(new ScrollColumn(rows), 128, 64, 0, false, 0);
    }
};

#endif
