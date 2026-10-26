#ifndef APP_WIFI_TOOLS_H
#define APP_WIFI_TOOLS_H

#include "../src/NanoUI.h"
#include "../src/nanoui_assets.h"
#include "wifi_hack_core.h"
// #include "app_wifi_attack.h" // 未在本菜单使用，保留源码供后续开发。
#include "app_wifi_beacon.h"
#include "app_wifi_sniff.h"
#include "app_mac_radar.h"
#include "app_ble_sync.h"

static const uint8_t icon_log_tools[]      = { 0x00,0x00,0x07,0xe0,0x08,0x10,0x10,0x08,0x20,0x04,0x20,0x84,0x40,0x82,0x40,0x82,0x40,0x82,0x40,0xc2,0x20,0x04,0x20,0x04,0x10,0x08,0x08,0x10,0x07,0xe0,0x00,0x00 };
static const uint8_t icon_deauth_tools[]   = { 0x00,0x00,0x3f,0xfc,0x20,0x04,0x20,0x04,0x22,0x04,0x21,0x04,0x20,0x84,0x21,0x44,0x22,0x24,0x20,0x04,0x20,0x7c,0x20,0x04,0x20,0x04,0x3f,0xfc,0x00,0x00,0x00,0x00 };
static const uint8_t icon_spectrum_tools[] = { 0x01,0x80,0x03,0xc0,0x07,0xe0,0x0c,0x30,0x18,0x18,0x30,0x0c,0x3f,0xfc,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x00,0x00,0x00,0x00 };

using namespace NanoUI;

// 日志阅读器
#if 0 // 固定示例日志，没有真实数据入口，暂不编译。
class AppTextReader : public Application {
public:
    const char* getAppName() override { return "TEXT READER"; }
    Widget* buildUI() override {
        std::vector<Widget*> items;
        items.push_back(new Text("Capturing Traffic...", 12));
        items.push_back(new SizedBox(0, 10));
        items.push_back(new Text("> [OK] Handshake found.", 12));
        items.push_back(new Text("> SSID: Target_AP", 12));
        items.push_back(new Spacer());
        
        std::vector<Widget*> btn_row;
        btn_row.push_back(new Spacer());
        btn_row.push_back(new Button(new Text("[ EXIT ]", 12), [](){ AppManager::exitCurrentApp(); }));
        btn_row.push_back(new Spacer());
        items.push_back(new Row(btn_row));

        return new Container(new Column(items), 128, 64);
    }
};
#endif

class WifiToolsMenuApp : public Application {
public:
    const char* getAppName() override { return "网络工具"; }
    Widget* buildUI() override {
        std::vector<Widget*> list_items;
        
        struct ToolDef { const uint8_t* icon; const char* t; const char* d; VoidCallback cb; };
        std::vector<ToolDef> tools;
        tools.push_back({icon_log_tools, "抓包", "握手监听", []() { AppManager::launch(new AppWifiSniffer()); }});
        tools.push_back({icon_deauth_tools, "信标", "广播测试", []() { AppManager::launch(new AppWifiBeacon()); }});
        tools.push_back({icon_spectrum_tools, "雷达", "设备追踪", []() { AppManager::launch(new AppMacRadar()); }});
        tools.push_back({icon_spectrum_tools, "频谱", "信道流量", []() { AppManager::launch(new AppSpectrum()); }});
        tools.push_back({icon_log_tools, "同步", "记录中转", []() { AppManager::launch(new AppBleSync()); }});

        for (const auto& tool : tools) {
            std::vector<Widget*> col_items;
            col_items.push_back(new Text(tool.t, 8));
            col_items.push_back(new Text(tool.d, 8));
            
            std::vector<Widget*> row_items;
            row_items.push_back(new SizedBox(4, 0));
            row_items.push_back(new Image(tool.icon, 16, 16));
            row_items.push_back(new SizedBox(4, 0));
            row_items.push_back(new Column(col_items));
            
            list_items.push_back(new Button(new Container(new Row(row_items), 76, 28, 0, true, 4), tool.cb));
            list_items.push_back(new SizedBox(0, 4));
        }

        list_items.push_back(new SizedBox(0, 4));
        list_items.push_back(new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));

        return new Container(new Row(std::vector<Widget*>{
            new SizedBox(4, 0),
            new Container(new Column(std::vector<Widget*>{new Spacer(), new AnimatedImage(asset_my_media, true), new Spacer()}), 38, 56, 1, true, 4),
            new SizedBox(6, 0),
            new Container(new ScrollColumn(list_items), 76, 64, 0, false, 0)
        }), 128, 64, 0, false, 0);
    }
};

#endif
