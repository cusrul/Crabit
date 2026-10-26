#ifndef APP_CLOCK_H
#define APP_CLOCK_H

#include <NanoUI/NanoUI.h>
#include <NanoUI/core/sensor_manager.h>
#include <NanoUI/core/sensor_types.h>
#include <string>
#include <stdio.h>
#include <time.h> // Include standard time for the simulator

// Check if we are building for the simulator
#ifdef NANOUI_SIMULATOR 
    // Mock the required types and functions for the desktop build
    typedef struct {
        uint8_t Hours;
        uint8_t Minutes;
        uint8_t Seconds;
    } RTC_TimeTypeDef;

    typedef struct {
        uint8_t Month;
        uint8_t Date;
        uint8_t WeekDay;
    } RTC_DateTypeDef;

    #define HAL_OK 0
    #define RTC_FORMAT_BIN 0
    int hrtc = 0; // Mock handle

    int HAL_RTC_GetTime(int* hrtc, RTC_TimeTypeDef* sTime, int format) {
        time_t rawtime;
        struct tm * timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        
        sTime->Hours = timeinfo->tm_hour;
        sTime->Minutes = timeinfo->tm_min;
        sTime->Seconds = timeinfo->tm_sec;
        return HAL_OK;
    }

    int HAL_RTC_GetDate(int* hrtc, RTC_DateTypeDef* sDate, int format) {
        time_t rawtime;
        struct tm * timeinfo;
        time(&rawtime);
        timeinfo = localtime(&rawtime);
        
        sDate->Month = timeinfo->tm_mon + 1;
        sDate->Date = timeinfo->tm_mday;
        sDate->WeekDay = timeinfo->tm_wday == 0 ? 7 : timeinfo->tm_wday;
        return HAL_OK;
    }

    size_t xPortGetFreeHeapSize() {
        return 1024 * 1024; // Mock 1MB free heap
    }
#else
    // Hardware build includes
    #include "rtc.h" 
    #include "FreeRTOS.h" 
#endif
extern Application* createLauncherApp();

// --- 顶部状态栏图标 ---
static const uint8_t ico_chip[] = { 0x3c, 0x42, 0x99, 0xbd, 0xbd, 0x99, 0x42, 0x3c }; 

// =========================================================
// 迷你实体电池积木 (适配顶部状态栏)
// =========================================================
class RenderDynamicBattery : public RenderObject {
public:
    float* pct; 
    RenderDynamicBattery(float* p) : pct(p) {}
    
    void performLayout() override { size = {14, 8}; } // 缩小尺寸，更精致
    
    void paintWorker(Canvas* canvas, Point p) override {
        // 画电池外壳和正极头 (y坐标稍微下沉一点，和文字对齐)
        int by = p.y + 1;
        canvas->drawRect(p.x, by, 12, 6, C_WHITE); 
        canvas->drawLine(p.x + 12, by + 2, p.x + 12, by + 3, C_WHITE);
        
        // 计算内部填充
        int fillWidth = (int)((*pct) * 10.0f);
        if (fillWidth > 10) fillWidth = 10;
        
        if (fillWidth > 0) {
            canvas->fillRect(p.x + 1, by + 1, fillWidth, 4, C_WHITE);
        } else {
            // 空电量画条斜线警告
            canvas->drawLine(p.x + 2, by + 1, p.x + 9, by + 4, C_WHITE);
        }
    }
};

class DynamicBattery : public Widget {
    float* pct;
public:
    DynamicBattery(float* p) : pct(p) {}
    RenderObject* createRenderObject() override { return new RenderDynamicBattery(pct); }
};

// =========================================================
// 表盘应用
// =========================================================
class ClockApp : public Application {
private:
    std::string hStr = "00", mStr = "00", sStr = "00";
    std::string dateStr = "01-01", dayStr = "MON";
    std::string ramStr = "0K", batPctStr = "100%";
    
    float batProgress = 1.0f; 
    uint32_t last_update = 0;
    bool colon_blink = true; 

    const char* weekdays[7] = {"MON", "TUE", "WED", "THU", "FRI", "SAT", "SUN"};

public:
    const char* getAppName() override { return "CLOCK"; }

    void onTick() override {
        uint32_t now = hal_get_ticks();
        
        // 每 500ms 刷新一次
        if (now - last_update > 500) {
            last_update = now;
            colon_blink = !colon_blink; 
            
            // 1. 获取 RTC 时间
            RTC_TimeTypeDef sTime = {0};
            RTC_DateTypeDef sDate = {0};
            if (HAL_RTC_GetTime(&hrtc, &sTime, RTC_FORMAT_BIN) == HAL_OK) {
                HAL_RTC_GetDate(&hrtc, &sDate, RTC_FORMAT_BIN);
                
                char buf[16];
                snprintf(buf, 8, "%02d", sTime.Hours);   hStr = buf;
                snprintf(buf, 8, "%02d", sTime.Minutes); mStr = buf;
                snprintf(buf, 8, "%02d", sTime.Seconds); sStr = buf;
                
                snprintf(buf, 16, "%02d/%02d", sDate.Month, sDate.Date); dateStr = buf;
                int wDay = sDate.WeekDay;
                if(wDay >= 1 && wDay <= 7) dayStr = weekdays[wDay - 1];
            }

            // 2. 获取电量
            auto* batt = NanoUI::SensorManager::getInstance().getSensor("BATTERY");
            if (batt) {
                const auto* bdata = static_cast<const NanoUI::SensorDataBattery*>(batt->getData());
                batProgress = bdata->percent / 100.0f;
                char bbuf[8]; snprintf(bbuf, 8, "%d%%", bdata->percent);
                batPctStr = bbuf;
            }

            // 3. 获取可用内存
            size_t freeHeap = xPortGetFreeHeapSize();
            char rbuf[10]; snprintf(rbuf, 10, "%uK", freeHeap / 1024);
            ramStr = rbuf;
            
            if(WindowManager::instance) WindowManager::instance->markDirty();
        }
    }

    Widget* buildUI() override {
        return new Button(
            new Container(
                new Column({
                    // --- 1. 顶部：系统状态栏 (全弹性分配) ---
                    new Row({
                        new SizedBox(4, 0),
                        new Text(dateStr.c_str(), 12),
                        new SizedBox(4, 0),
                        new Text(dayStr.c_str(), 12),
                        
                        new Spacer(), // 自动把状态推到两端
                        
                        new DynamicBattery(&batProgress),
                        new SizedBox(2, 0),
                        new Text(batPctStr.c_str(), 12),
                        new SizedBox(4, 0),
                    }),

                    new Spacer(), // 纵向弹性空间：把时间往中间挤

                    // --- 2. 中部：大字时间 (比例排版自动居中) ---
                    new Row({
                        new Spacer(), 
                        
                        new SlidingText(&hStr, 24),
                        new Text(colon_blink ? ":" : " ", 24),
                        new SlidingText(&mStr, 24),
                        
                        new SizedBox(4, 0),
                        
                        // 秒数：不再使用魔法数字对齐，改用 Spacer 垂直撑开
                        new Column({
                            new Spacer(1, 12), // 顶部弹性，至少预留半个大字高度
                            new Container(
                                new Row({ 
                                    new Spacer(), 
                                    new SlidingText(&sStr, 12), 
                                    new Spacer() 
                                }), 
                                18, 11, 0, true, 2
                            ),
                            new Spacer()
                        }),
                        
                        new Spacer()
                    }),

                    new Spacer(), // 纵向弹性空间

                    // --- 3. 底部：装饰条 (全弹性分配) ---
                    new Row({
                        new SizedBox(4, 0),
                        new Text("SYS.CORE", 12),
                        new Spacer(), // 弹性推开
                        new Text("[ OPEN MENU ]", 12),
                        new SizedBox(4, 0)
                    }),
                    new SizedBox(0, 1), // 底部极小留白
                }),
                128, 64, 0, false, 0
            ),
            []() { AppManager::launch(createLauncherApp()); }
        );
    }
};

#endif