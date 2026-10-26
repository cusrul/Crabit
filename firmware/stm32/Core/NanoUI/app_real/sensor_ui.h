#ifndef APP_SENSOR_UI_H
#define APP_SENSOR_UI_H

#include "NanoUI/NanoUI.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

class RenderSensorValueText : public RenderObject {
   public:
    const char* text;
    uint8_t textSize;

    RenderSensorValueText(const char* t, uint8_t s) : text(t), textSize(s) {}

    void performLayout() override { size = {46, 16}; }

    void paintWorker(Canvas* canvas, Point p) override {
        const char* value = (text && text[0]) ? text : "N/A";
        int16_t text_w = get_utf8_string_width(value, textSize, 0);
        int16_t draw_x = p.x + size.width - text_w;
        if (draw_x < p.x)
            draw_x = p.x;
        canvas->drawText(draw_x, p.y + 2, value, textSize, false, 0);
    }
};

class SensorValueText : public Widget {
   public:
    const char* text;
    uint8_t textSize;

    SensorValueText(const char* t, uint8_t s = 8) : text(t), textSize(s) {}

    RenderObject* createRenderObject() override {
        return new RenderSensorValueText(text, textSize);
    }
};

inline void SetSensorText(char* dst, size_t cap, const char* src) {
    if (!dst || cap == 0)
        return;
    if (!src)
        src = "";
    if (strcmp(dst, src) == 0)
        return;
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
    if (WindowManager::instance)
        WindowManager::instance->markDirty();
}

inline void FormatFixed0(char* dst, size_t cap, float value, const char* suffix = "") {
    long scaled = lroundf(value);
    snprintf(dst, cap, "%ld%s", scaled, suffix ? suffix : "");
}

inline void FormatFixed1(char* dst, size_t cap, float value, const char* suffix = "") {
    long scaled = lroundf(value * 10.0f);
    bool neg = scaled < 0;
    unsigned long abs_scaled = (unsigned long)(neg ? -scaled : scaled);
    unsigned long whole = abs_scaled / 10;
    unsigned long frac = abs_scaled % 10;
    snprintf(dst, cap, neg ? "-%lu.%01lu%s" : "%lu.%01lu%s", whole, frac, suffix ? suffix : "");
}

inline void FormatFixed2(char* dst, size_t cap, float value, const char* suffix = "") {
    long scaled = lroundf(value * 100.0f);
    bool neg = scaled < 0;
    unsigned long abs_scaled = (unsigned long)(neg ? -scaled : scaled);
    unsigned long whole = abs_scaled / 100;
    unsigned long frac = abs_scaled % 100;
    snprintf(dst, cap, neg ? "-%lu.%02lu%s" : "%lu.%02lu%s", whole, frac, suffix ? suffix : "");
}

inline Widget* BuildSensorTitle(const char* title, const char* tail = nullptr) {
    NanoUI::EmbeddedVector<Widget*> row;
    row.push_back(new Spacer());
    row.push_back(new Text(title, 8));
    if (tail && tail[0] != '\0') {
        row.push_back(new SizedBox(6, 0));
        row.push_back(new Text(tail, 8));
    }
    row.push_back(new Spacer());
    return new Row(row);
}

inline Widget* BuildSensorInfoRow(const char* label, Widget* valueWidget) {
    NanoUI::EmbeddedVector<Widget*> row;
    row.push_back(new SizedBox(8, 0));
    row.push_back(new Container(new Text(label, 8), 54, 16, 0, false));
    row.push_back(new Spacer());
    row.push_back(valueWidget);
    row.push_back(new SizedBox(8, 0));
    return new Button(new Row(row), []() {});
}

inline Widget* BuildSensorValueText(const char* value) {
    return new SensorValueText(value, 8);
}

inline Widget* BuildSensorMenuRow(const char* label, const char* value,
                                  VoidCallback onPressed = nullptr, bool showChevron = false) {
    (void)onPressed;
    (void)showChevron;
    return BuildSensorInfoRow(label, BuildSensorValueText(value));
}

inline Widget* BuildSensorBackRow() {
    return new Row(NanoUI::EmbeddedVector<Widget*>{
        new Spacer(),
        new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2),
                   []() { AppManager::exitCurrentApp(); }),
        new Spacer()});
}

#endif
