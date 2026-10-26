#ifndef APP_CLOCK_NEW_H
#define APP_CLOCK_NEW_H

#include "launcher.h"
#include <string>
#include <cstdio>
#include <vector>

// =========================================================
// 系统面板 (带圆角与边框)
// =========================================================
inline Widget* Panel(Widget* child, int16_t w=0, int16_t h=0) {
    return new Container(child, w, h, 2, true, 2);
}

// ==========================================
// 横向电池指示
// ==========================================
class RenderHorizontalBattery : public RenderObject {
public:
    void performLayout() override { size = { 16, 8 }; }
    void paintWorker(Canvas* canvas, Point p) override {
        int pct = DataHub::shared.battery.percent;
        canvas->drawRect(p.x, p.y, 14, 8, C_WHITE); 
        canvas->drawLine(p.x + 14, p.y + 2, p.x + 14, p.y + 5, C_WHITE);
        int fillWidth = (int)(pct / 100.0f * 12.0f);
        if (fillWidth > 0) canvas->fillRect(p.x + 1, p.y + 1, fillWidth, 6, C_WHITE);
    }
};

class HorizontalBattery : public Widget {
public:
    RenderObject* createRenderObject() override { return new RenderHorizontalBattery(); }
};

// ==========================================
// Cyber Deck 表盘
// ==========================================
class CyberClockApp : public Application {
private:
    std::string timeStr = "00:00";
    std::string secStr = "00";
    std::string dateStr = "06/05 FRI";
    std::string ramStr = "52K";
    std::string uptimeStr = "UP 00:45";
    
    uint32_t last_update = 0;
    bool colon_blink = true;

public:
    const char* getAppName() override { return "CYBER_CLOCK"; }

    void onTick() override {
        uint32_t now = hal_get_ticks();
        if (now - last_update > 500) {
            last_update = now;
            colon_blink = !colon_blink;

            RTC_TimeTypeDef sTime = {0};
            RTC_DateTypeDef sDate = {0};
            if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK) {
                HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
                char buf[32];
                // 强制使用标准格式，确保居中稳定
                snprintf(buf, sizeof(buf), "%02d%c%02d", sTime.Hours, colon_blink ? ':' : ' ', sTime.Minutes);
                timeStr = buf;
                snprintf(buf, sizeof(buf), "%02d", sTime.Seconds);
                secStr = buf;
                
                const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
                snprintf(buf, sizeof(buf), "%02d/%02d %s", sDate.Month, sDate.Date, days[sDate.WeekDay % 7]);
                dateStr = buf;
            }
            if(WindowManager::instance) WindowManager::instance->markDirty();
        }
    }

    Widget* buildUI() override {
        return new Container(
            new Column({
                // --- 1. 顶栏：系统指标 (分块式设计) ---
                new Row({
                    new SizedBox(2, 0),
                    new Text(dateStr.c_str(), 10),
                    new Spacer(),
                    new Text("RAM:", 8),
                    new Text(ramStr.c_str(), 10),
                    new SizedBox(4, 0),
                    new HorizontalBattery(),
                    new SizedBox(2, 0)
                }),

                new SizedBox(0, 4), // 固定间隔

                // --- 2. 中间：巨幕时间核心 (使用 Expanded 抢占最大空间) ---
                new Expanded(
                    new Container(
                        new Row({
                            new Spacer(),
                            new Text(timeStr.c_str(), 24), // 24px 核心点阵字
                            new SizedBox(6, 0),
                            new Column({
                                new Spacer(),
                                new Container(new Text(secStr.c_str(), 10), 16, 12, 1, true, 2),
                                new Spacer()
                            }),
                            new Spacer()
                        }),
                        128, 0, 0, false, 0 // 宽度占满，高度随 Expanded
                    )
                ),

                new SizedBox(0, 4),

                // --- 3. 底栏：动态面板 ---
                new Container(
                    new Row({
                        new SizedBox(4, 0),
                        new Text(uptimeStr.c_str(), 10),
                        new SizedBox(6, 0),
                        new Expanded(new MarqueeText("STATUS: CORE.ACTIVE // I2C.BUS.OK // LINK.ESTABLISHED", 10)),
                        new SizedBox(4, 0),
                        new Button(
                            new Container(new Text("MENU", 10), 0, 0, 1, true, 2),
                            []() { AppManager::launch(createLauncherApp()); }
                        ),
                        new SizedBox(2, 0)
                    }),
                    128, 16, 0, false, 0
                ),
                new SizedBox(0, 2)
            }),
            128, 64, 0, false, 0
        );
    }
};

inline Application* createCyberClockApp() { return new CyberClockApp(); }

#endif
