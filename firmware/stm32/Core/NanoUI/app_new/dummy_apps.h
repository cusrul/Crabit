#ifndef APP_DUMMY_H
#define APP_DUMMY_H

#include <NanoUI/NanoUI.h>

class SettingsApp : public Application {
public:
    const char* getAppName() override { return "SETTINGS"; }
    Widget* buildUI() override {
        return new Container(new Column({
            new Spacer(),
            new Row({new Spacer(), new Text("--- CONFIG ---", 12), new Spacer()}),
            new Spacer(),
            new Row({new Spacer(), new Button(new Text("< BACK", 12), [](){ AppManager::exitCurrentApp(); }), new Spacer()}),
            new Spacer()
        }), 128, 64, 0, false, 0);
    }
};

class HackerApp : public Application {
public:
    const char* getAppName() override { return "HACKER"; }
    Widget* buildUI() override {
        return new Container(new Column({
            new Spacer(),
            new Row({new Spacer(), new Text(" TERMINAL ", 12), new Spacer()}),
            new Spacer(),
            new Row({new Spacer(), new Button(new Text("< BACK", 12), [](){ AppManager::exitCurrentApp(); }), new Spacer()}),
            new Spacer()
        }), 128, 64, 0, false, 0);
    }
};

// 4. 游戏 App (真正的跨平台独占模式)
class GameApp : public Application {
public:
    const char* getAppName() override { return "GAME"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    // 引擎已经把抽象的 canvas 和 input 传给你了，直接用它们！
    void runExclusive(Canvas* canvas, InputDriver* input) override {
        bool running = true;
        int x = 64, y = 32; 
        uint32_t last_frame = hal_get_ticks(); // 使用抽象的时间！

        while(running) {
            // 1. 读取按键控制 (只依赖抽象 InputDriver)
            UIEvent e = input->poll();
            if(e.type == EVT_BACK) running = false; 
            if(e.type == EVT_LEFT)  x -= 2;
            if(e.type == EVT_RIGHT) x += 2;
            if(e.type == EVT_UP)    y -= 2;
            if(e.type == EVT_DOWN)  y += 2;

            // 2. 使用非阻塞的时间抽象控制帧率 (约 30ms 刷新一次)
            uint32_t now = hal_get_ticks();
            if (now - last_frame >= 30) {
                last_frame = now;
                
                // 3. 清屏并画蛇头 (只依赖抽象 Canvas)
                canvas->clear();
                canvas->drawRoundRect(x, y, 4, 4, 1, C_WHITE); 
                canvas->display();
            }
        }
    }
};

#endif