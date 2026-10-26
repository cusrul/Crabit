#ifndef APP_HELLO_H
#define APP_HELLO_H

#include "NanoUI/NanoUI.h"

class HelloApp : public Application {
   public:
    const char* getAppName() override { return "HELLO"; }
    Widget* buildUI() override {
        // 使用最基础的布局实现居中显示
        return new Container(
            new Row({new Spacer(), new Text("Crabit OS Baseline", 8), new Spacer()}), 128, 64, 0,
            false, 0);
    }
};

#endif
