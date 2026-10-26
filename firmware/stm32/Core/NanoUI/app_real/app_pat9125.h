#ifndef APP_PAT9125_H
#define APP_PAT9125_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"
#include "sensor_ui.h"
#include "stm_sensor_service.h"
#include <cstdio>
#include <vector>

class PATApp : public Application {
   private:
    char str_dx[24];
    char str_dy[24];
    char str_agc[24];
    char str_mode[24];
    char str_state[24];

   public:
    PATApp() {
        snprintf(str_dx, sizeof(str_dx), "N/A");
        snprintf(str_dy, sizeof(str_dy), "N/A");
        snprintf(str_agc, sizeof(str_agc), "N/A");
        snprintf(str_mode, sizeof(str_mode), "N/A");
        snprintf(str_state, sizeof(str_state), "N/A");
    }

    const char* getAppName() override { return "PAT9125"; }

    void onTick() override {
        StmSensorSnapshot snapshot{};
        if (!STM32_GetUiSensorSnapshot(snapshot))
            return;
        const auto* data = &snapshot.optical;
        if (!data || !data->is_valid) {
            SetSensorText(str_dx, sizeof(str_dx), "N/A");
            SetSensorText(str_dy, sizeof(str_dy), "N/A");
            SetSensorText(str_agc, sizeof(str_agc), "N/A");
            SetSensorText(str_mode, sizeof(str_mode), "OFFLINE");
            SetSensorText(str_state, sizeof(str_state), "N/A");
            return;
        }

        char buf[24];
        snprintf(buf, sizeof(buf), "%d", data->dx);
        SetSensorText(str_dx, sizeof(str_dx), buf);
        snprintf(buf, sizeof(buf), "%d", data->dy);
        SetSensorText(str_dy, sizeof(str_dy), buf);
        snprintf(buf, sizeof(buf), "%d", (int)data->agc);
        SetSensorText(str_agc, sizeof(str_agc), buf);
        snprintf(buf, sizeof(buf), "%d", data->mode);
        SetSensorText(str_mode, sizeof(str_mode), buf);
        SetSensorText(str_state, sizeof(str_state), data->lifted ? "LIFT" : "TRACK");
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> rows;

        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorTitle("激光传感", "PAT9125"));
        rows.push_back(new SizedBox(0, 10));
        rows.push_back(BuildSensorMenuRow("横向位移", str_dx));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("纵向位移", str_dy));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("增益", str_agc));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("曝光", str_mode));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorMenuRow("追踪状态", str_state));
        rows.push_back(new SizedBox(0, 16));
        rows.push_back(BuildSensorBackRow());
        rows.push_back(new SizedBox(0, 6));

        return new Container(new ScrollColumn(rows), 128, 64, 0, false, 0);
    }
};

#endif
