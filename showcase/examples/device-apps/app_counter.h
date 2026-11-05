#pragma once

#include <NanoUI.h>
#include <cstdio>

class CounterApp : public Application {
    unsigned count = 0;
    char value[20] = "Count: 0";

    void setCount(unsigned next) {
        count = next;
        std::snprintf(value, sizeof(value), "Count: %u", count);
        if (WindowManager::instance)
            WindowManager::instance->markNeedsLayout();
    }

public:
    const char* getAppName() override { return "Counter"; }
    unsigned currentCount() const { return count; }

    Widget* buildUI() override {
        return new Container(new Column({
            new Text("My first App", 8),
            new SizedBox(0, 6),
            new Text(value, 8),
            new SizedBox(0, 6),
            new Row({
                new Button(new Container(new Text("+1", 8), 44, 18),
                           [this]() { setCount(count < 999 ? count + 1 : 999); }),
                new SizedBox(8, 0),
                new Button(new Container(new Text("Reset", 8), 44, 18),
                           [this]() { setCount(0); })
            })
        }), 128, 64);
    }
};
