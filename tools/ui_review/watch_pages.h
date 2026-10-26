#pragma once

#include <NanoUI.h>

namespace CrabitUiReview {
namespace detail {

enum WeatherGlyphKind { kSunny, kCloudy, kRainy };

class RenderWeatherGlyph : public RenderObject {
   public:
    WeatherGlyphKind kind;
    int16_t extent;

    RenderWeatherGlyph(WeatherGlyphKind glyphKind, int16_t glyphExtent)
        : kind(glyphKind), extent(glyphExtent) {}

    void performLayout() override { size = constraints.constrainSize({extent, extent}); }

    void paintWorker(Canvas* canvas, Point origin) override {
        const int16_t edge = size.width < size.height ? size.width : size.height;
        if (edge < 8)
            return;

        if (kind == kSunny) {
            const int16_t cx = origin.x + edge / 2;
            const int16_t cy = origin.y + edge / 2;
            const int16_t inner = edge / 4;
            const int16_t outer = edge / 2 - 1;
            canvas->drawCircle(cx, cy, inner, C_WHITE);
            canvas->drawLine(cx, origin.y, cx, cy - inner - 1, C_WHITE);
            canvas->drawLine(cx, cy + inner + 1, cx, origin.y + edge - 1, C_WHITE);
            canvas->drawLine(origin.x, cy, cx - inner - 1, cy, C_WHITE);
            canvas->drawLine(cx + inner + 1, cy, origin.x + edge - 1, cy, C_WHITE);
            canvas->drawLine(cx - outer / 2, cy - outer / 2, cx - inner, cy - inner, C_WHITE);
            canvas->drawLine(cx + inner, cy + inner, cx + outer / 2, cy + outer / 2, C_WHITE);
            canvas->drawLine(cx + inner, cy - inner, cx + outer / 2, cy - outer / 2, C_WHITE);
            canvas->drawLine(cx - outer / 2, cy + outer / 2, cx - inner, cy + inner, C_WHITE);
            return;
        }

        const int16_t baseline = origin.y + edge * 3 / 4;
        const int16_t radius = edge / 5;
        canvas->drawCircle(origin.x + edge / 3, baseline - radius / 2, radius, C_WHITE);
        canvas->drawCircle(origin.x + edge / 2, origin.y + edge * 2 / 5, radius + 1, C_WHITE);
        canvas->drawCircle(origin.x + edge * 2 / 3, baseline - radius / 2, radius, C_WHITE);
        canvas->drawLine(origin.x + 2, baseline, origin.x + edge - 3, baseline, C_WHITE);
        if (kind == kRainy) {
            const int16_t rainTop = origin.y + edge * 3 / 4 + 1;
            const int16_t rainBottom = origin.y + edge - 1;
            canvas->drawLine(origin.x + edge / 3, rainTop, origin.x + edge / 3 - 1, rainBottom,
                             C_WHITE);
            canvas->drawLine(origin.x + edge * 2 / 3, rainTop, origin.x + edge * 2 / 3 - 1,
                             rainBottom, C_WHITE);
        }
    }
};

class WeatherGlyph : public Widget {
   public:
    WeatherGlyphKind kind;
    int16_t extent;

    explicit WeatherGlyph(WeatherGlyphKind glyphKind, int16_t glyphExtent = 12)
        : kind(glyphKind), extent(glyphExtent) {}

    RenderObject* createRenderObject() override { return new RenderWeatherGlyph(kind, extent); }
};

class RenderBatteryGlyph : public RenderObject {
   public:
    void performLayout() override { size = constraints.constrainSize({14, 8}); }

    void paintWorker(Canvas* canvas, Point origin) override {
        if (size.width < 14 || size.height < 8)
            return;
        canvas->drawRect(origin.x, origin.y + 1, 11, 6, C_WHITE);
        canvas->fillRect(origin.x + 2, origin.y + 3, 7, 2, C_WHITE);
        canvas->fillRect(origin.x + 12, origin.y + 3, 2, 2, C_WHITE);
    }
};

class BatteryGlyph : public Widget {
   public:
    RenderObject* createRenderObject() override { return new RenderBatteryGlyph(); }
};

class RenderClockDial : public RenderObject {
   public:
    void performLayout() override { size = constraints.constrainSize({52, 52}); }

    void paintWorker(Canvas* canvas, Point origin) override {
        if (size.width < 52 || size.height < 52)
            return;

        static const int8_t outerX[12] = {0, 10, 16, 19, 16, 10, 0, -10, -16, -19, -16, -10};
        static const int8_t outerY[12] = {-19, -16, -10, 0, 10, 16, 19, 16, 10, 0, -10, -16};
        static const int8_t innerX[12] = {0, 8, 14, 16, 14, 8, 0, -8, -14, -16, -14, -8};
        static const int8_t innerY[12] = {-16, -14, -8, 0, 8, 14, 16, 14, 8, 0, -8, -14};
        const int16_t cx = origin.x + 26;
        const int16_t cy = origin.y + 26;

        canvas->drawCircle(cx, cy, 21, C_WHITE);
        for (int i = 0; i < 12; ++i) {
            canvas->drawLine(cx + innerX[i], cy + innerY[i], cx + outerX[i], cy + outerY[i],
                             C_WHITE);
        }

        // 12:48:36: hour at 24 degrees, minute at 288, second at 216.
        canvas->drawLine(cx, cy, cx - 11, cy + 15, C_WHITE);
        canvas->drawLine(cx, cy, cx - 17, cy - 6, C_WHITE);
        canvas->drawLine(cx, cy, cx + 5, cy - 12, C_WHITE);
        canvas->fillCircle(cx, cy, 2, C_WHITE);
    }
};

class ClockDial : public Widget {
   public:
    RenderObject* createRenderObject() override { return new RenderClockDial(); }
};

inline uint8_t smallFontSize(int requested) {
    if (requested == 8 || requested == 10 || requested == 12)
        return static_cast<uint8_t>(requested);
    return 10;
}

inline Widget* screen(Widget* content) {
    return new Container(content, 128, 64);
}

inline Widget* batteryPercent(uint8_t font) {
    return new Row({new BatteryGlyph(), new SizedBox(2, 0), new Text("76%", font)});
}

inline Widget* timeOnly(uint8_t font) {
    return new Row({new Text("12:48", 24), new Text(":36", font), new Spacer()});
}

inline Widget* dateAndBattery(uint8_t font) {
    return new Row({new Text("10/21周三", font), new Spacer(), batteryPercent(font)});
}

inline Widget* dailyCondition(uint8_t font, bool stale) {
    return new Row({new WeatherGlyph(kSunny, 24), new SizedBox(4, 0),
                    new Column({new Text(stale ? "今日晴（较旧）" : "今日晴", font),
                                new Text("低18° 高26°", font)})});
}

inline Widget* faceInformation(uint8_t font) {
    return screen(new Column({
        timeOnly(font),
        new SizedBox(0, 1),
        dateAndBattery(font),
        new SizedBox(0, 1),
        new Row({new Text("状态", font), new Spacer(), new Text("蓝牙关", font)}),
        new SizedBox(0, 1),
        new Row({new Text("菜单", font), new Spacer(), new Text("设置", font)}),
    }));
}

inline Widget* faceMinimal(uint8_t font) {
    return screen(new Column({
        timeOnly(font),
        new SizedBox(0, 1),
        dateAndBattery(font),
    }));
}

inline Widget* faceWeather(uint8_t font) {
    return screen(new Column({
        timeOnly(font),
        new SizedBox(0, 1),
        dateAndBattery(font),
        new SizedBox(0, 1),
        dailyCondition(font, false),
    }));
}

inline Widget* faceAnalog(uint8_t font) {
    return screen(new Row({
        new ClockDial(),
        new SizedBox(5, 0),
        new Column({new Text("12:48:36", font), new Text("10/21周三", font), batteryPercent(font)}),
    }));
}

inline Widget* weatherDaily(uint8_t font, bool stale, bool empty) {
    Widget* header =
        new Row({new Text("天气", font), new Spacer(), new Text("10/21周三", font),
                 new SizedBox(2, 0), new Text(empty ? "未同步" : (stale ? "较旧" : "正常"), font)});
    if (empty) {
        return screen(new Column({
            header,
            new SizedBox(0, 3),
            new Container(new Text("暂无天气数据", font), 128, 20),
            new SizedBox(0, 3),
            new Text("降水：未提供", font),
            new Text("请在手机端刷新", font),
        }));
    }

    return screen(new Column({
        header,
        new SizedBox(0, 1),
        dailyCondition(font, stale),
        new SizedBox(0, 1),
        new Text("降水 10%", font),
        new Text(stale ? "上次更新 昨日" : "更新 12:30", font),
    }));
}

inline Widget* weatherThreeDays(uint8_t font, bool stale, bool empty) {
    Widget* header = new Row({new Text("未来三天", font), new Spacer(),
                              new Text(stale ? "更新较久" : "更新正常", font)});
    if (empty) {
        return screen(new Column({
            header,
            new SizedBox(0, 5),
            new Text("暂无天气数据", font),
            new SizedBox(0, 3),
            new Text("降水：未提供", font),
        }));
    }

    return screen(new Column({
        header,
        new SizedBox(0, 3),
        new Row({new Text("10/21", font), new Spacer(), new Text("晴", font), new Spacer(),
                 new Text("低18° 高26°", font)}),
        new SizedBox(0, 3),
        new Row({new Text("10/22", font), new Spacer(), new Text("多云", font), new Spacer(),
                 new Text("低17° 高25°", font)}),
        new SizedBox(0, 3),
        new Row({new Text("10/23", font), new Spacer(), new Text("雨", font), new Spacer(),
                 new Text("低16° 高22°", font)}),
    }));
}

}  // namespace detail

inline Widget* buildFace(int variant, int smallFont) {
    const uint8_t font = detail::smallFontSize(smallFont);
    switch (variant) {
        case 1:
            return detail::faceMinimal(font);
        case 2:
            return detail::faceWeather(font);
        case 3:
            return detail::faceAnalog(font);
        case 0:
        default:
            return detail::faceInformation(font);
    }
}

inline Widget* buildWeather(int variant, int smallFont, bool stale, bool empty) {
    const uint8_t font = detail::smallFontSize(smallFont);
    return variant == 1 ? detail::weatherThreeDays(font, stale, empty)
                        : detail::weatherDaily(font, stale, empty);
}

}  // namespace CrabitUiReview
