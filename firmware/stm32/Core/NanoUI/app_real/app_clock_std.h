#ifndef APP_CLOCK_STD_H
#define APP_CLOCK_STD_H

#include "NanoUI/NanoUI.h"
#include "clock_context.h"

static const uint8_t ico_chip[] = {0x3c, 0x42, 0x99, 0xbd, 0xbd, 0x99, 0x42, 0x3c};

class RenderDynamicBattery : public RenderObject {
   public:
    float* pct;
    RenderDynamicBattery(float* p) : pct(p) {}
    void performLayout() override { size = {14, 8}; }
    void paintWorker(Canvas* canvas, Point p) override {
        int by = p.y + 1;
        canvas->drawRect(p.x, by, 12, 6, C_WHITE);
        canvas->drawLine(p.x + 12, by + 2, p.x + 12, by + 3, C_WHITE);
        int fillWidth = (int)((*pct) * 10.0f);
        if (fillWidth > 10)
            fillWidth = 10;
        if (fillWidth > 0)
            canvas->fillRect(p.x + 1, by + 1, fillWidth, 4, C_WHITE);
        else
            canvas->drawLine(p.x + 2, by + 1, p.x + 9, by + 4, C_WHITE);
    }
};

class DynamicBattery : public Widget {
    float* pct;

   public:
    DynamicBattery(float* p) : pct(p) {}
    RenderObject* createRenderObject() override { return new RenderDynamicBattery(pct); }
};

// 馃専 鏆撮湶缁欑瀹剁殑缁勮鍑芥暟
inline Widget* buildFaceStandard(ClockContext ctx) {
    return new Column(NanoUI::EmbeddedVector<Widget*>{
        new SizedBox(0, 2),
        new Row(NanoUI::EmbeddedVector<Widget*>{
            new SizedBox(2, 0),
            new Text(ctx.dateStr->c_str(), 1),
            new SizedBox(4, 0),
            new Text(ctx.dayStr->c_str(), 1),
            new Spacer(),
            new Image(ico_chip, 8, 8),
            new SizedBox(2, 0),
            new Text(ctx.ramStr->c_str(), 1),
            new SizedBox(4, 0),
            new DynamicBattery(ctx.batProgress),
            new SizedBox(2, 0),
            new Text(ctx.batPctStr->c_str(), 1),
            new SizedBox(2, 0),
        }),
        new SizedBox(0, 12),
        new Row(NanoUI::EmbeddedVector<Widget*>{
            new Spacer(), new SlidingText(ctx.hStr, 2),
            new Column(NanoUI::EmbeddedVector<Widget*>{
                new SizedBox(0, 2), new Text(*ctx.colon_blink ? ":" : " ", 2), new Spacer()}),
            new SlidingText(ctx.mStr, 2), new SizedBox(3, 0),
            new Column(NanoUI::EmbeddedVector<Widget*>{
                new Spacer(),
                new Container(
                    new Row(NanoUI::EmbeddedVector<Widget*>{
                        new SizedBox(1, 0), new SlidingText(ctx.sStr, 1), new SizedBox(1, 0)}),
                    18, 12, 2, true, 2),
            }),
            new Spacer()}),
        new SizedBox(0, 10),
        new Row(NanoUI::EmbeddedVector<Widget*>{new SizedBox(4, 0), new Text("SYS.OPT", 1),
                                                new Spacer(), new Text("[ PRESS ]", 1),
                                                new SizedBox(4, 0)}),
        new SizedBox(0, 2),
    });
}

#endif
