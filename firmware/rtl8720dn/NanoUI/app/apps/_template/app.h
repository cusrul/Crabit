#ifndef CRABIT_APP_EXAMPLE_H
#define CRABIT_APP_EXAMPLE_H

// Copied from apps/_template/app.h. Keep the include depth for
// apps/<category>/<id>/app.h; adjust only if the package moves.
// Register the class once in app/arduboy30_registry.h; no app.json is needed.
#include "../../../../src/NanoUI.h"

class ExampleApp : public Application {
public:
    const char* getAppName() override { return "Example App"; }

    Widget* buildUI() override {
        using namespace NanoUI;
        return new Container(new Column(std::vector<Widget*>{
            new Spacer(),
            new Text("Example App", 8),
            new Spacer(),
            new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2),
                       []() { AppManager::exitCurrentApp(); })
        }), 128, 64, 0, false, 0);
    }
};

#endif
