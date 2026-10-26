#ifndef CRABIT_NANOUI_SIM_FLASH_APP_H
#define CRABIT_NANOUI_SIM_FLASH_APP_H
#include "NanoUI/NanoUI.h"
class FlashApp : public Application {
public:
    const char* getAppName() override { return "UPDATE_PREVIEW"; }
    Widget* buildUI() override {
        return new Container(new Column({new Spacer(), new Text("固件烧录", 8),
            new Text("仅界面预览", 8), new Text("不会连接硬件", 8), new Spacer(),
            new Button(new Text("返回", 8), [] { AppManager::exitCurrentApp(); })}), 128, 64);
    }
};
#endif
