#ifndef APP_BATTERY_H
#define APP_BATTERY_H

#include "NanoUI/NanoUI.h"
#include "NanoUI/core/sensor_manager.h"
#include "NanoUI/core/sensor_types.h"  // 馃専 蹇呴』寮曞叆鏁版嵁濂戠害
#include <stdio.h>
#include "stm_sensor_service.h"

class RenderBattery : public RenderObject {
   private:
    float prev_v = 0;
    bool is_charging = false;

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
        int percent = data->percent;

        if (v_now - prev_v > 0.05f && prev_v > 1.0f)
            is_charging = true;
        else if (prev_v - v_now > 0.05f)
            is_charging = false;
        prev_v = v_now;

        char buf[32];
        canvas->drawText(p.x + 5, p.y + 2, is_charging ? "MODE: CHARGING" : "MODE: BATTERY", 1,
                         is_charging);

        int v_int = (int)v_now;
        int v_frac = (int)((v_now - v_int) * 100);
        sprintf(buf, "Volt: %d.%02dV", v_int, (v_frac < 0 ? 0 : v_frac));
        canvas->drawText(p.x + 5, p.y + 20, buf, 1);

        int i_est = is_charging ? (v_now < 4.1f ? 500 : 150) : -45;
        sprintf(buf, "I_est: %s%dmA", (i_est > 0 ? "+" : ""), i_est);
        canvas->drawText(p.x + 5, p.y + 35, buf, 1);

        sprintf(buf, "Prcnt: %d%%", percent);
        canvas->drawText(p.x + 5, p.y + 50, buf, 1);

        canvas->drawRect(p.x + 90, p.y + 20, 20, 35, C_WHITE);
        canvas->fillRect(p.x + 95, p.y + 17, 10, 3, C_WHITE);

        int bar_h = (percent * 31) / 100;
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
