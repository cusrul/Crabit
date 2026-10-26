#ifndef CRABIT_APP_WEATHER_H
#define CRABIT_APP_WEATHER_H
#include "NanoUI/NanoUI.h"
#include "network_weather.h"
#include "main.h"
#include <stdio.h>

class WeatherApp : public Application {
    NanoUI::NanoUIText row;
    NanoUI::NanoUIText statusLine;
    NanoUI::NanoUIText timeLine;
    unsigned first = 0;
    uint32_t lastDraw = 0;

   public:
    const char* getAppName() override { return "天气"; }
    void onTick() override {
        if (uint32_t(HAL_GetTick() - lastDraw) < 250)
            return;
        lastDraw = HAL_GetTick();
        statusLine = Crabit::NetworkWeather::phoneLabel();
        const uint32_t updated = Crabit::NetworkWeather::lastUpdated();
        CrabitWeather::Civil stamp{};
        if (updated &&
            CrabitWeather::civil(updated, Crabit::NetworkWeather::lastUpdatedOffset(), stamp)) {
            char update[32];
            snprintf(update, sizeof(update), "上次 %02u/%02u %02u:%02u", stamp.month, stamp.day,
                     stamp.hour, stamp.minute);
            timeLine = update;
        } else
            timeLine = "暂无更新时间";
        if (first >= Crabit::NetworkWeather::phoneCount())
            row = "暂无天气缓存";
        else {
            char line[64];
            const auto& d = Crabit::NetworkWeather::phoneDay(first);
            snprintf(line, sizeof(line), "%02lu/%02lu %s %d~%d度 雨%u%%",
                     (unsigned long)(d.date / 100 % 100), (unsigned long)(d.date % 100),
                     Crabit::NetworkWeather::condition(d.code), d.low / 10, d.high / 10, d.rain);
            row = line;
        }
        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }
    Widget* buildUI() override {
        using namespace NanoUI;
        return new Container(
            new Column(NanoUI::EmbeddedVector<Widget*>{
                new Text("手机天气", 8), new SlidingText(&statusLine, 8),
                new SlidingText(&timeLine, 8), new SlidingText(&row, 8),
                new Row(NanoUI::EmbeddedVector<Widget*>{
                    new Button(new Text("手机刷新", 8),
                               []() { Popup::showToast("请在手机端打开天气并刷新"); }),
                    new Spacer(),
                    new Button(new Text("翻页", 8),
                               [this]() {
                                   first = first + 1 < Crabit::NetworkWeather::phoneCount()
                                               ? first + 1
                                               : 0;
                                   lastDraw = HAL_GetTick() - 250;
                                   onTick();
                               }),
                    new Spacer(),
                    new Button(new Text("返回", 8), []() { AppManager::exitCurrentApp(); })})}),
            128, 64);
    }
};
#endif
