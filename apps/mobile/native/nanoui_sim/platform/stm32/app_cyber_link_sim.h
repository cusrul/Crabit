#ifndef CRABIT_NANOUI_SIM_CYBER_LINK_APP_H
#define CRABIT_NANOUI_SIM_CYBER_LINK_APP_H
#include "NanoUI/NanoUI.h"
class CyberLinkApp : public Application {
public:
    const char* getAppName() override { return "RADIO_PREVIEW"; }
    Widget* buildUI() override {
        return new Container(new Column({new Spacer(), new Text("BW16 无线功能", 8),
            new Text("实体手表入口", 8), new Text("预览不启动无线", 8), new Spacer(),
            new Button(new Text("返回", 8), [] { AppManager::exitCurrentApp(); })}), 128, 64);
    }
};
#endif
