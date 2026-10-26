#ifndef APP_DCDC_CTRL_H
#define APP_DCDC_CTRL_H

#include "NanoUI/NanoUI.h"
#include "axp2101.h"
#include <cstdio>
#include <vector>
#include <string>

extern "C" {
    extern I2C_HandleTypeDef hi2c1;
}

// 供不带捕获的 Lambda 转换为 C 风格回调时使用的指针。
static void* g_dcdc_app_ptr = nullptr;

class DCDC3CtrlApp : public Application {
private:
    bool is_enabled = false;
    int current_mv = 0; 

    void refreshState() {
        uint8_t ctrl = 0;
        if (AXP2101_ReadReg(&hi2c1, AXP2101_DC_ONOFF_DVM_CTRL, &ctrl) == HAL_OK) {
            is_enabled = (ctrl & 0x04) != 0;
        }
        current_mv = (int)AXP2101_GetDC3Voltage(&hi2c1);
    }

    static void onApplyThunk() {
        if (g_dcdc_app_ptr) {
            auto app = static_cast<DCDC3CtrlApp*>(g_dcdc_app_ptr);
            AXP2101_SetDC3Voltage(&hi2c1, (uint16_t)app->current_mv);
            AppManager::exitCurrentApp();
        }
    }

    static void onToggleThunk(bool val) {
        if (g_dcdc_app_ptr) {
            static_cast<DCDC3CtrlApp*>(g_dcdc_app_ptr)->is_enabled = val;
            AXP2101_SetDC3Enable(&hi2c1, val);
        }
    }

public:
    const char* getAppName() override { return "DCDC3"; }

    void onCreate() override {
        g_dcdc_app_ptr = this;
        refreshState();
    }

    void onDestroy() override {
        if (g_dcdc_app_ptr == this) g_dcdc_app_ptr = nullptr;
    }

    Widget* buildUI() override {
        NanoUI::EmbeddedVector<Widget*> items;
        
        items.push_back(new SizedBox(0, 10));
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{ new Spacer(), new Text("DCDC3 CONFIG", 8), new Spacer() }));
        items.push_back(new SizedBox(0, 8));

        // 1. 开关控制
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{
            new SizedBox(12, 0),
            new Text("Power", 8),
            new Spacer(),
            new Switch(is_enabled, onToggleThunk),
            new SizedBox(12, 0)
        }));

        items.push_back(new SizedBox(0, 10));

        // 2. 电压调整
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{
            new SizedBox(12, 0),
            new Text("Volt(mV)", 8),
            new Spacer(),
            new NumberPicker(&current_mv, 500, 3400),
            new SizedBox(12, 0)
        }));

        items.push_back(new SizedBox(0, 15));

        // 3. 应用并返回按钮
        items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{
            new Spacer(),
            new Button(new Container(new Text(" APPLY ", 8), 60, 18, 1, true, 2), onApplyThunk),
            new Spacer()
        }));

        return new Container(new ScrollColumn(items), 128, 64, 0, false, 0);
    }
};

#endif
