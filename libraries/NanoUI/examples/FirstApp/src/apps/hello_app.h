#pragma once

#include <NanoUI.h>

class HelloApp : public Application {
public:
    HelloApp() : enabled(false) {}

    const char* getAppName() override { return title; }

    Widget* buildUI() override {
        return new Column({
            new Text(title, 12),
            new Row({new Text("Enabled", 12), new Switch(enabled, onEnabledChanged)})
        });
    }

    bool isEnabled() const { return enabled; }

private:
    const char title[32] = "NanoUI 示例";
    bool enabled;

    static void onEnabledChanged(bool value) { (void)value; }
};
