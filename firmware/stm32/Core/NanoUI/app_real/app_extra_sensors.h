#ifndef APP_EXTRA_SENSORS_H
#define APP_EXTRA_SENSORS_H

#include "NanoUI/NanoUI.h"
#include "sensor_ui.h"
#include <vector>
#include <cstdio>
#include <cstring>
#include "stm_sensor_service.h"

// ==========================================
// 传感器数值显示组件；避免格式化字符串以减少 Flash 占用。
// ==========================================
class RenderDataValue : public RenderObject {
   public:
    const char* sensorName;
    uint8_t type;  // 0:BMP_P, 1:BMP_T
    RenderDataValue(const char* name, uint8_t t) : sensorName(name), type(t) {}
    void performLayout() override { size = {46, 16}; }
    void paintWorker(Canvas* canvas, Point p) override {
        StmSensorSnapshot snapshot{};
        if (!STM32_GetUiSensorSnapshot(snapshot) || strcmp(sensorName, "BMP280") != 0) {
            canvas->drawText(p.x + 28, p.y + 4, "N/A", 1);
            return;
        }
        char buf[16] = {0};
        if (type < 2) {
            const auto* d = &snapshot.pressure;
            if (!d->is_valid) {
                canvas->drawText(p.x + 28, p.y + 4, "N/A", 1);
                return;
            }
            if (type == 0)
                FormatFixed1(buf, sizeof(buf), d->pressure);
            else
                FormatFixed1(buf, sizeof(buf), d->temperature);
        }
        int16_t text_w = get_utf8_string_width(buf, 1, 0);
        int16_t draw_x = p.x + size.width - text_w;
        if (draw_x < p.x)
            draw_x = p.x;
        canvas->drawText(draw_x, p.y + 4, buf, 1, false, 0);
    }
};

class DataValue : public Widget {
   public:
    const char* name;
    uint8_t type;
    DataValue(const char* n, uint8_t t) : name(n), type(t) {}
    RenderObject* createRenderObject() override { return new RenderDataValue(name, type); }
};

class ExtraSensorsApp : public Application {
   public:
    const char* getAppName() override { return "EXTRA_SNS"; }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> rows;

        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorTitle("扩展传感", "DEBUG"));
        rows.push_back(new SizedBox(0, 10));
        rows.push_back(BuildSensorMenuRow("BMP280 状态", "在线"));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorInfoRow("气压", new DataValue("BMP280", 0)));
        rows.push_back(new SizedBox(0, 4));
        rows.push_back(BuildSensorInfoRow("温度", new DataValue("BMP280", 1)));
        rows.push_back(new SizedBox(0, 16));
        rows.push_back(BuildSensorBackRow());
        rows.push_back(new SizedBox(0, 6));

        return new Container(new ScrollColumn(rows), 128, 64, 0, false, 0);
    }
};

#endif
