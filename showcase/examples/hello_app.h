#pragma once
#include <NanoUI.h>

// Tutorial replacement for FirstApp's header. State belongs to the App.
class HelloApp : public Application {
public:
    const char* getAppName() override { return "First Panel"; }
    Widget* buildUI() override {
        return new Column({
            new Text("First Panel", 12),
            new Row({new Text("Enabled", 12), new Switch(enabled)}),
            new Row({new Text("Sound", 12), new Switch(sound)})
        });
    }
    bool isEnabled() const { return enabled; }
private:
    bool enabled = false;
    bool sound = false;
};
