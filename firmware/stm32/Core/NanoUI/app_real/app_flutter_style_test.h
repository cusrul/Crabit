#ifndef APP_FLUTTER_STYLE_TEST_H
#define APP_FLUTTER_STYLE_TEST_H

#include "NanoUI/NanoUI.h"

class FlutterStyleTestApp : public Application {
public:
    const char* getAppName() override { return "FlutterTest"; }

    Widget* buildUI() override {
        // Look how clean this is now! Like Flutter!
        return new Container(
            new ScrollColumn({
                new Row({
                    new Spacer(),
                    new Text("FLUTTER STYLE", 8),
                    new Spacer()
                }),
                new SizedBox(0, 10),
                new Row({
                    new SizedBox(10, 0),
                    new Text("Item 1", 8),
                    new Spacer(),
                    new Text("100%", 8)
                }),
                new SizedBox(0, 5),
                new Row({
                    new SizedBox(10, 0),
                    new Text("Item 2", 8),
                    new Spacer(),
                    new Text("OK", 8)
                }),
                new SizedBox(0, 15),
                new Row({
                    new Spacer(),
                    new Button(
                        new Container(new Text(" CLICK ME ", 1), 60, 18, 1, true, 2),
                        []() { Popup::showToast("Clicked!"); }
                    ),
                    new Spacer()
                })
            }),
            128, 64, 0, false, 0
        );
    }
};

#endif
