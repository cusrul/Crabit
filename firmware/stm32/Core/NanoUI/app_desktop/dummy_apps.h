#ifndef DUMMY_APPS_H
#define DUMMY_APPS_H

#include "NanoUI/NanoUI.h"

// 桌面端 App 样式占位符
class PlaceholderApp : public Application {
private:
    const char* name;
public:
    PlaceholderApp(const char* n) : name(n) {}
    const char* getAppName() override { return name; }
    
    Widget* buildUI() override {
        return new Button(
            new Container(
                new Column({
                    new Spacer(),
                    new Row({new Spacer(), new Text(name, 1), new Spacer()}),
                    new Row({new Spacer(), new Text("[ PLACEHOLDER ]", 1), new Spacer()}),
                    new SizedBox(0, 10),
                    new Row({new Spacer(), new Text("< CLICK TO BACK", 1), new Spacer()}),
                    new Spacer()
                }), 128, 64, 0, true, 2
            ),
            [](){ AppManager::exitCurrentApp(); }
        );
    }
};

#endif
