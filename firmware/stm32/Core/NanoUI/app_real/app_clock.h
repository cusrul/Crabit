#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include "NanoUI/NanoUI.h"
#include "rtc.h"
#include "stm_sensor_service.h"
#include "network_weather.h"
#include "sys_config.h"
#include "companion_ble.h"
#include "components/clock_face_action.h"
#include "components/inverted_surface.h"
#include "../../../../shared/ui/animated_time.h"
#include <cmath>
#include <cstdio>
#include <cstring>

extern Application* createLauncherApp();

// Small display primitive; battery data is copied once by ClockApp, never read
// from a driver in paint. Unknown is distinct from an empty or full battery.
class RenderClockBattery : public RenderObject {
    const int* percentage;

   public:
    explicit RenderClockBattery(const int* value) : percentage(value) {}
    void performLayout() override { size = constraints.constrainSize({14, 8}); }
    void paintWorker(Canvas* canvas, Point p) override {
        ScopedClip clip(canvas, {p.x, p.y, int16_t(p.x + size.width), int16_t(p.y + size.height)});
        canvas->drawRect(p.x, p.y + 1, 12, 6, C_WHITE);
        canvas->drawLine(p.x + 12, p.y + 3, p.x + 12, p.y + 4, C_WHITE);
        if (*percentage < 0) {
            canvas->drawLine(p.x + 2, p.y + 5, p.x + 9, p.y + 2, C_WHITE);
        } else {
            const int width = *percentage * 10 / 100;
            if (width)
                canvas->fillRect(p.x + 1, p.y + 2, width, 4, C_WHITE);
        }
    }
};
class ClockBattery : public Widget {
    const int* percentage;

   public:
    explicit ClockBattery(const int* value) : percentage(value) {}
    RenderObject* createRenderObject() override { return new RenderClockBattery(percentage); }
};

class ClockApp : public Application {
    NanoUI::NanoUIText date_text = "--/--";
    NanoUI::NanoUIText battery_text = "--%";
    NanoUI::NanoUIText weather_text = "天气未同步";
    NanoUI::NanoUIText weather_condition = "天气未同步", weather_range = "--~--C";
    NanoUI::NanoUIText temperature_text = "手表温度--";
    NanoUI::NanoUIText bluetooth_text = "蓝牙关";
    int battery_percentage = -1;
    uint32_t date_key = 0, last_update = 0;
    Crabit::WatchFaces::TimeTransition time_transition;

    void readTime(uint32_t now, bool animate) {
        RTC_TimeTypeDef time{};
        RTC_DateTypeDef date{};
        if (HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK)
            return;
        // Date releases the STM32 RTC shadow registers after a Time read.
        if (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)
            return;
        time_transition.set(time.Hours, time.Minutes, now, animate);
        date_key = (2000u + date.Year) * 10000u + date.Month * 100u + date.Date;
        const char* days[] = {"日", "一", "二", "三", "四", "五", "六"};
        char text[32];
        std::snprintf(text, sizeof(text), "%02u/%02u 周%s", unsigned(date.Month),
                      unsigned(date.Date), days[date.WeekDay <= 7 ? date.WeekDay % 7 : 0]);
        date_text = text;
    }

    void readStatus(uint32_t now) {
        const char* ble_status = Crabit::CompanionBle::status();
        if (!Crabit::CompanionBle::active())
            bluetooth_text = "蓝牙关";
        else if (std::strcmp(ble_status, "CONNECTED") == 0 ||
                 std::strcmp(ble_status, "AUTHENTICATED") == 0)
            bluetooth_text = "蓝牙已连";
        else
            bluetooth_text = "蓝牙开";
        StmSensorSnapshot snapshot{};
        const bool available = STM32_GetUiSensorSnapshot(snapshot);
        const auto& battery = snapshot.battery;
        char text[64];
        battery_percentage = -1;
        if (available && battery.is_valid && now - battery.timestamp <= 300000u &&
            battery.percent >= 0 && battery.percent <= 100) {
            battery_percentage = battery.percent;
            std::snprintf(text, sizeof(text), battery.is_charging ? "充%d%%" : "%d%%",
                          battery_percentage);
            battery_text = text;
        } else
            battery_text = "--%";

        const auto& pressure = snapshot.pressure;
        if (available && pressure.is_valid && now - pressure.timestamp <= 30000u &&
            std::isfinite(pressure.temperature) && pressure.temperature >= -40.0f &&
            pressure.temperature <= 85.0f) {
            const int deci = int(std::round(pressure.temperature * 10.0f));
            const unsigned magnitude = unsigned(deci < 0 ? -deci : deci);
            std::snprintf(text, sizeof(text), "手表温度%s%u.%uC", deci < 0 ? "-" : "",
                          magnitude / 10, magnitude % 10);
            temperature_text = text;
        } else
            temperature_text = "手表温度--";

        const unsigned count = Crabit::NetworkWeather::phoneCount();
        if (!count) {
            weather_text = "天气未同步";
            weather_condition = "天气未同步";
            weather_range = "--~--C";
            return;
        }
        unsigned selected = 0;
        for (unsigned i = 0; i < count && i < CrabitWeather::MaxDays; ++i)
            if (Crabit::NetworkWeather::phoneDay(i).date == date_key) {
                selected = i;
                break;
            }
        const auto& day = Crabit::NetworkWeather::phoneDay(selected);
        // Forecast range is not a live outdoor-temperature reading.
        auto roundedDegrees = [](int deci) {
            return deci < 0 ? -((-deci + 5) / 10) : (deci + 5) / 10;
        };
        std::snprintf(text, sizeof(text), "%s %s %d~%dC",
                      day.date == date_key ? "天气" : "缓存天气",
                      Crabit::NetworkWeather::condition(day.code), roundedDegrees(day.low),
                      roundedDegrees(day.high));
        weather_text = text;
        std::snprintf(text, sizeof(text), "%s %s", day.date == date_key ? "天气" : "缓存",
                      Crabit::NetworkWeather::condition(day.code));
        weather_condition = text;
        std::snprintf(text, sizeof(text), "%d~%dC", roundedDegrees(day.low),
                      roundedDegrees(day.high));
        weather_range = text;
    }

    static void openMenu() { AppManager::launch(createLauncherApp()); }

    Widget* statusRow(bool showBluetooth) {
        return new Row({new SlidingText(&date_text, 8), new Spacer(),
                        showBluetooth ? static_cast<Widget*>(new SlidingText(&bluetooth_text, 8))
                                      : static_cast<Widget*>(new SizedBox(0, 0)),
                        new SizedBox(6, 0), new ClockBattery(&battery_percentage),
                        new SizedBox(2, 0), new SlidingText(&battery_text, 8)});
    }

    static Widget* menuButton() {
        return new Button(new DecoratedBox(new ConstrainedBox(new Align(new Text("菜单", 8)),
                                                              BoxConstraints::tight(24, 12)),
                                           false, 2),
                          openMenu);
    }

    Widget* invertedBarFace() {
        return new ClockFaceAction(
            new ConstrainedBox(
                new Column(
                    {new Padding(statusRow(false), Insets{2, 1, 2, 1}),
                     new Expanded(
                         new Align(new Crabit::WatchFaces::MinimalTimeDigits(&time_transition),
                                   Alignment::Center, true, true)),
                     new InvertedSurface(new Padding(
                         new Column({new Row({new SlidingText(&bluetooth_text, 8), new Spacer(),
                                              new SlidingText(&weather_text, 8)}),
                                     new SizedBox(0, 2),
                                     new Row({new SlidingText(&temperature_text, 8), new Spacer(),
                                              new Text("菜单", 8)})}),
                         Insets{2, 1, 2, 1}))}),
                BoxConstraints::tight(128, 64)),
            openMenu);
    }

    Widget* splitFace() {
        return new ConstrainedBox(
            new Padding(
                new Column(
                    {statusRow(true), new SizedBox(0, 6),
                     new Expanded(new Row(
                         {new Align(new Crabit::WatchFaces::MinimalTimeDigits(&time_transition, 2),
                                    Alignment::Center, false, true),
                          new SizedBox(8, 0), new DecoratedBox(new SizedBox(1, 30)),
                          new SizedBox(7, 0),
                          new Expanded(new Align(
                              new Column({new SlidingText(&weather_condition, 8),
                                          new SizedBox(0, 5), new SlidingText(&weather_range, 8)}),
                              Alignment::CenterLeft, true, true))})),
                     new Row({new SlidingText(&temperature_text, 8), new Spacer(), menuButton()})}),
                Insets{2, 1, 2, 2}),
            BoxConstraints::tight(128, 64));
    }

    Widget* stackedFace() {
        return new ConstrainedBox(
            new Padding(
                new Column({statusRow(false), new SizedBox(0, 5),
                            new Expanded(new Row(
                                {new Column({new Crabit::WatchFaces::MinimalTimeDigits(
                                                 &time_transition, 3, 0, 2),
                                             new SizedBox(0, 3),
                                             new Crabit::WatchFaces::MinimalTimeDigits(
                                                 &time_transition, 3, 3, 2)}),
                                 new SizedBox(18, 0),
                                 new Expanded(new Column(
                                     {new SlidingText(&weather_condition, 8), new SizedBox(0, 3),
                                      new SlidingText(&weather_range, 8), new SizedBox(0, 5),
                                      new SlidingText(&temperature_text, 8), new SizedBox(0, 2),
                                      new Row({new SlidingText(&bluetooth_text, 8), new Spacer(),
                                               menuButton()})}))}))}),
                Insets{2, 1, 2, 2}),
            BoxConstraints::tight(128, 64));
    }

   public:
    const char* getAppName() override { return "CLOCK"; }
    void onCreate() override {
        const uint32_t now = hal_get_ticks();
        readTime(now, false);
        readStatus(now);
    }
    void onTick() override {
        const uint32_t now = hal_get_ticks();
        if (now - last_update < 500)
            return;
        last_update = now;
        const uint32_t previous_transition = time_transition.started;
        readTime(now, true);
        const uint8_t face = Crabit::validWatchFace(sys_cfg.watch_face);
        if (face == static_cast<uint8_t>(Crabit::WatchFace::Liquid) ||
            face == static_cast<uint8_t>(Crabit::WatchFace::Minimal)) {
            if (time_transition.started != previous_transition && WindowManager::instance)
                WindowManager::instance->markDirty();
            return;
        }
        readStatus(now);
        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }

    Widget* buildUI() override {
        const uint32_t now = hal_get_ticks();
        readTime(now, false);
        const uint8_t face = Crabit::validWatchFace(sys_cfg.watch_face);
        if (face == static_cast<uint8_t>(Crabit::WatchFace::Liquid) ||
            face == static_cast<uint8_t>(Crabit::WatchFace::Minimal)) {
            return new ClockFaceAction(
                new Container(
                    new Crabit::WatchFaces::AnimatedTimeDisplay(
                        face == static_cast<uint8_t>(Crabit::WatchFace::Minimal), &time_transition),
                    128, 64),
                []() { AppManager::launch(createLauncherApp()); });
        }
        readStatus(now);
        if (face == static_cast<uint8_t>(Crabit::WatchFace::Split))
            return splitFace();
        if (face == static_cast<uint8_t>(Crabit::WatchFace::Stacked))
            return stackedFace();
        return invertedBarFace();
    }
};

#endif
