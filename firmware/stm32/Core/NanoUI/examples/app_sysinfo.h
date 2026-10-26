#ifndef APP_SYSINFO_H
#define APP_SYSINFO_H

#include <NanoUI/NanoUI.h>
#include <NanoUI/core/data_hub.h> // 引入数据枢纽
#include <string>
#include <stdio.h>
#include <vector>

// =========================================================
// 内存阵列碎片图
// =========================================================
class RenderMemGrid : public RenderObject {
public:
    float progress; 
    RenderMemGrid(float p) : progress(p) {}
    void performLayout() override { size = { 100, 14 }; } 
    void paintWorker(Canvas* canvas, Point p) override {
        int totalBlocks = 18; 
        int usedBlocks = (int)(progress * totalBlocks);
        for (int i = 0; i < totalBlocks; i++) {
            int col = i % 9; int row = i / 9;
            int bx = p.x + col * 11; int by = p.y + row * 7;
            if (i < usedBlocks) canvas->fillRect(bx, by, 9, 5, C_WHITE);
            else { canvas->drawRect(bx, by, 9, 5, C_WHITE); canvas->drawPixel(bx + 4, by + 2, C_WHITE); }
        }
    }
};
class MemGrid : public Widget {
public:
    float pct; MemGrid(float p) : pct(p) {}
    RenderObject* createRenderObject() override { return new RenderMemGrid(pct); }
};

// =========================================================
// 系统监控 App (解耦版)
// =========================================================
class SysInfoApp : public Application {
private:
    std::string uptimeStr = "00:00:00";
    std::string heapStr = "0.0K";
    std::string pctStr = "0%";
    float ramPercent = 0.0f;

    Widget* buildInfoCard(const char* title, Widget* content, int height) {
        return new Button(new Container(new Column({
            new Row({ new SizedBox(2,0), new Text(title, 12), new Spacer() }),
            new Spacer(), 
            new Row({ new Spacer(), content, new Spacer() }),
            new Spacer()
        }), 112, height, 4, true, 3), [](){} );
    }

public:
    const char* getAppName() override { return "SYSINFO"; }

    void onTick() override {
        // 从 DataHub 镜像读取数据，不直接读硬件寄存器
        uint32_t now = DataHub::shared.uptimeMs;
        uint32_t sec = now / 1000;
        uint32_t m = (sec / 60) % 60;
        uint32_t h = (sec / 3600);
        sec = sec % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, sec);
        uptimeStr = buf;

        float total = (float)DataHub::shared.totalHeap;
        float free = (float)DataHub::shared.freeHeap;
        ramPercent = (total - free) / total;
        
        snprintf(buf, sizeof(buf), "FREE:%u.%uK", DataHub::shared.freeHeap / 1024, (DataHub::shared.freeHeap % 1024) * 10 / 1024);
        heapStr = buf;
        snprintf(buf, sizeof(buf), "USE:%d%%", (int)(ramPercent * 100));
        pctStr = buf;

        // 通知系统刷新
        if (WindowManager::instance) WindowManager::instance->markDirty();
    }

    Widget* buildUI() override {
        std::vector<Widget*> items;
        items.push_back(new SizedBox(0, 6));
        items.push_back(new Row({ new Spacer(), new Text("- SYSTEM CORE -", 12), new Spacer() }));
        items.push_back(new SizedBox(0, 10));
        items.push_back(buildInfoCard("UPTIME", new SlidingText(&uptimeStr, 24), 48));
        items.push_back(new SizedBox(0, 8));
        items.push_back(buildInfoCard("RAM BLOCKS", new Column({
            new MemGrid(ramPercent),
            new SizedBox(0, 6),
            new Row({ new Text(heapStr.c_str(), 12), new Spacer(), new Text(pctStr.c_str(), 12) })
        }), 76));
        items.push_back(new SizedBox(0, 12));
        items.push_back(new Row({ new Spacer(), new Button(new Container(new Text(" BACK TO OS ", 12), 0, 16, 0, true, 2), [](){ AppManager::exitCurrentApp(); }), new Spacer() }));
        return new Container(new ScrollColumn(items), 128, 64, 0, false, 0);
    }
};

#endif