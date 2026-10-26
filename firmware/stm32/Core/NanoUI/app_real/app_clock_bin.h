#ifndef APP_CLOCK_BIN_H
#define APP_CLOCK_BIN_H

#include "NanoUI/NanoUI.h"
#include "clock_context.h"

class RenderBigBinClock : public RenderObject {
   public:
    RTC_TimeTypeDef* t;
    RenderBigBinClock(RTC_TimeTypeDef* t) : t(t) {}
    void performLayout() override { size = {128, 64}; }
    void paintWorker(Canvas* canvas, Point p) override {
        NanoUI::NanoUIText hrStr;
        for (int i = 4; i >= 0; i--)
            hrStr += ((t->Hours & (1 << i)) ? "1" : "0");
        NanoUI::NanoUIText mnStr;
        for (int i = 5; i >= 0; i--)
            mnStr += ((t->Minutes & (1 << i)) ? "1" : "0");
        canvas->drawText(34, 12, hrStr.c_str(), 2, false);
        canvas->drawText(28, 38, mnStr.c_str(), 2, false);
    }
};

class BigBinClockWidget : public Widget {
    RTC_TimeTypeDef* t;

   public:
    BigBinClockWidget(RTC_TimeTypeDef* t) : t(t) {}
    RenderObject* createRenderObject() override { return new RenderBigBinClock(t); }
};

// 馃専 鏆撮湶缁欑瀹剁殑缁勮鍑芥暟
inline Widget* buildFaceBinary(ClockContext ctx) {
    return new BigBinClockWidget(ctx.rtcTime);
}

#endif
