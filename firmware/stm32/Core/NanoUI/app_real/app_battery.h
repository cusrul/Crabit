#ifndef APP_BATTERY_H
#define APP_BATTERY_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"  // 馃専 蹇呴』寮曞叆鏁版嵁濂戠害
#include <stdio.h>
#include "stm_sensor_service.h"

class RenderBattery : public RenderObject {
   public:
    void performLayout() override { size = {128, 64}; }

    void paintWorker(Canvas* canvas, Point p) override {
        StmSensorSnapshot snapshot{};
        if (!STM32_GetUiSensorSnapshot(snapshot)) {
            canvas->drawText(p.x + 10, p.y + 25, "BATT SENSOR ERR", 1);
            return;
        }

        const NanoUI::SensorDataBattery* data = &snapshot.battery;

        float v_now = data->voltage;
        const int percent = data->percent;
        const bool percent_valid = data->is_valid && percent >= 0 && percent <= 100;

        const bool charge_valid = data->is_valid && data->charging_valid;
        const bool is_charging = charge_valid && data->is_charging;
        const bool voltage_valid = data->is_valid && data->voltage_valid;

        char buf[32];
        canvas->drawText(p.x + 5, p.y + 2, !charge_valid ? "MODE: N/A" :
                         (is_charging ? "MODE: CHARGING" : "MODE: BATTERY"), 1,
                         is_charging);

        int v_int = (int)v_now;
        int v_frac = (int)((v_now - v_int) * 100);
        if (voltage_valid) snprintf(buf, sizeof(buf), "Volt: %d.%02dV", v_int, (v_frac < 0 ? 0 : v_frac));
        else snprintf(buf, sizeof(buf), "Volt: N/A");
        canvas->drawText(p.x + 5, p.y + 20, buf, 1);

        int i_est = is_charging ? (v_now < 4.1f ? 500 : 150) : -45;
        if (charge_valid && voltage_valid)
            snprintf(buf, sizeof(buf), "I_est: %s%dmA", (i_est > 0 ? "+" : ""), i_est);
        else snprintf(buf, sizeof(buf), "I_est: N/A");
        canvas->drawText(p.x + 5, p.y + 35, buf, 1);

        if (percent_valid) snprintf(buf, sizeof(buf), "Prcnt: %d%%", percent);
        else snprintf(buf, sizeof(buf), "Prcnt: --%%");
        canvas->drawText(p.x + 5, p.y + 50, buf, 1);

        canvas->drawRect(p.x + 90, p.y + 20, 20, 35, C_WHITE);
        canvas->fillRect(p.x + 95, p.y + 17, 10, 3, C_WHITE);

        int bar_h = percent_valid ? (percent * 31) / 100 : 0;
        if (bar_h > 0) {
            canvas->fillRect(p.x + 92, p.y + 53 - bar_h, 16, bar_h, C_WHITE);
        }

        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }
};

class BatteryWidget : public Widget {
   public:
    RenderObject* createRenderObject() override { return new RenderBattery(); }
};

class BatteryApp : public Application {
   public:
    const char* getAppName() override { return "BATT"; }
    Widget* buildUI() override { return new Container(new BatteryWidget(), 128, 64, 0, false); }
};

#endif
