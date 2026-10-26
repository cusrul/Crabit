#ifndef APP_SYSINFO_H
#define APP_SYSINFO_H

#include "../src/NanoUI.h"
#include "../src/core/data_hub.h" 
#include <string>
#include <stdio.h>
#include <vector>

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

class SysInfoApp : public Application {
private:
    std::string uptimeStr = "00:00:00";
    std::string heapStr = "0.0K";
    std::string pctStr = "0%";
    float ramPercent = 0.0f;

    Widget* buildInfoCard(const char* title, Widget* content, int height) {
        std::vector<Widget*> col_items;
        
        std::vector<Widget*> row1_items;
        row1_items.push_back(new SizedBox(2,0));
        row1_items.push_back(new Text(title, 12));
        row1_items.push_back(new Spacer());
        col_items.push_back(new Row(row1_items));
        
        col_items.push_back(new Spacer());
        
        std::vector<Widget*> row2_items;
        row2_items.push_back(new Spacer());
        row2_items.push_back(content);
        row2_items.push_back(new Spacer());
        col_items.push_back(new Row(row2_items));
        
        col_items.push_back(new Spacer());
        
        return new Button(new Container(new Column(col_items), 112, height, 4, true, 3), [](){} );
    }

public:
    const char* getAppName() override { return "SYSINFO"; }

    void onTick() override {
        uint32_t now = DataHub::shared.uptimeMs;
        uint32_t sec = now / 1000;
        uint32_t m = (sec / 60) % 60;
        uint32_t h = (sec / 3600);
        sec = sec % 60;
        char buf[32];
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", (unsigned int)h, (unsigned int)m, (unsigned int)sec);
        uptimeStr = buf;

        float total = (float)DataHub::shared.totalHeap;
        float free = (float)DataHub::shared.freeHeap;
        ramPercent = (total - free) / total;
        
        snprintf(buf, sizeof(buf), "FREE:%u.%uK", (unsigned int)(DataHub::shared.freeHeap / 1024), (unsigned int)((DataHub::shared.freeHeap % 1024) * 10 / 1024));
        heapStr = buf;
        snprintf(buf, sizeof(buf), "USE:%d%%", (int)(ramPercent * 100));
        pctStr = buf;

        if (WindowManager::instance) WindowManager::instance->markDirty();
    }

    Widget* buildUI() override {
        std::vector<Widget*> items;
        items.push_back(new SizedBox(0, 6));
        
        std::vector<Widget*> title_row;
        title_row.push_back(new Spacer());
        title_row.push_back(new Text("- SYSTEM CORE -", 12));
        title_row.push_back(new Spacer());
        items.push_back(new Row(title_row));
        
        items.push_back(new SizedBox(0, 10));
        items.push_back(buildInfoCard("UPTIME", new SlidingText(&uptimeStr, 24), 48));
        items.push_back(new SizedBox(0, 8));
        
        std::vector<Widget*> ram_col_items;
        ram_col_items.push_back(new MemGrid(ramPercent));
        ram_col_items.push_back(new SizedBox(0, 6));
        std::vector<Widget*> ram_row;
        ram_row.push_back(new Text(heapStr.c_str(), 12));
        ram_row.push_back(new Spacer());
        ram_row.push_back(new Text(pctStr.c_str(), 12));
        ram_col_items.push_back(new Row(ram_row));
        
        items.push_back(buildInfoCard("RAM BLOCKS", new Column(ram_col_items), 76));
        items.push_back(new SizedBox(0, 12));
        
        std::vector<Widget*> back_row;
        back_row.push_back(new Spacer());
        back_row.push_back(new Button(new Container(new Text(" BACK TO OS ", 12), 0, 16, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));
        back_row.push_back(new Spacer());
        items.push_back(new Row(back_row));
        
        return new Container(new ScrollColumn(items), 128, 64, 0, false, 0);
    }
};

#endif
