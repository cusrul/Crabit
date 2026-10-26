#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H 

#include "NanoUI/NanoUI.h"
#include "NanoUI/nanoui_assets.h"
#if defined(NANOUI_SIMULATOR)
#include "simulator_product_apps.h"
#else
#include "app_clock.h"
#include "app_sensor.h"
#include "app_pat9125.h"
#include "app_bw16_flash.h"
#include "app_battery.h"
#include "app_sysinfo.h"
#include "app_time_set.h"
#include "app_settings.h" 
#include "app_coproc.h"
#include "app_cyber_link.h" 
#include "app_ble_link.h"
#include "app_weather.h"
#include "app_audio_settings.h"
#include "app_bmp280.h"
#include "app_axp2101.h"
#include "app_ws2812_control.h"
#endif

// =========================================================
// 16x16 像素图标。
// =========================================================
static const uint8_t icon_clk[]    = { 0x00,0x00,0x07,0xe0,0x08,0x10,0x10,0x08,0x20,0x04,0x20,0x84,0x40,0x82,0x40,0x82,0x40,0x82,0x40,0xc2,0x20,0x04,0x20,0x04,0x10,0x08,0x08,0x10,0x07,0xe0,0x00,0x00 };
static const uint8_t icon_set[]    = { 0x00,0x00,0x06,0x60,0x0f,0xf0,0x1f,0xf8,0x3c,0x3c,0x78,0x1e,0x70,0x0e,0x78,0x1e,0x3c,0x3c,0x1f,0xf8,0x0f,0xf0,0x06,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t icon_gam[]    = { 0x00,0x00,0x0f,0xf0,0x30,0x0c,0x40,0x02,0x48,0x12,0x48,0x12,0x40,0x02,0x48,0x12,0x40,0x02,0x40,0x02,0x40,0x02,0x30,0x0c,0x0f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t icon_snd[]    = { 0x00,0x00,0x01,0x80,0x03,0xc0,0x07,0xe0,0x1f,0xf0,0x1f,0xf8,0x1f,0xfc,0x1f,0xfc,0x1f,0xfc,0x1f,0xf8,0x1f,0xf0,0x07,0xe0,0x03,0xc0,0x01,0x80,0x00,0x00,0x00,0x00 };
static const uint8_t icon_folder_real[] = { 0x1f,0xf8,0x10,0x08,0x17,0xe8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x17,0xe8,0x10,0x08,0x1f,0xf8,0x00,0x00,0x00,0x00 };
static const uint8_t icon_term[]   = { 0x00,0x00,0x3f,0xfc,0x20,0x04,0x20,0x04,0x22,0x04,0x21,0x04,0x20,0x84,0x21,0x44,0x22,0x24,0x20,0x04,0x20,0x7c,0x20,0x04,0x20,0x04,0x3f,0xfc,0x00,0x00,0x00,0x00 };
static const uint8_t icon_link[]   = { 0x00,0x00,0x07,0xe0,0x18,0x18,0x20,0x04,0x40,0x02,0x41,0x82,0x43,0xc2,0x43,0xc2,0x43,0xc2,0x41,0x82,0x40,0x02,0x20,0x04,0x18,0x18,0x07,0xe0,0x00,0x00,0x00,0x00 };

struct MenuItemDef {
    const uint8_t* icon;
    const char* title;
    VoidCallback onClick;
};

// =========================================================
// 构建带图标与动画的菜单页。
// =========================================================
inline Widget* buildAnimMenu(const uint8_t* animAsset, std::initializer_list<MenuItemDef> items) {
    NanoUI::EmbeddedVector<Widget*> list_items;
    for (const auto& item : items) {
        list_items.push_back(new Button(new Container(new Row(NanoUI::EmbeddedVector<Widget*>{
            new SizedBox(6,0), new Text(item.title, 8), new Spacer(), new Text(">", 8), new SizedBox(6,0)
        }), 76, 22, 0, true, 4), item.onClick));
        list_items.push_back(new SizedBox(0, 4));
    }
    list_items.push_back(new SizedBox(0, 4));
    list_items.push_back(new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));

    return new Container(new Row(NanoUI::EmbeddedVector<Widget*>{
        new SizedBox(4, 0),
        new Container(new Column(NanoUI::EmbeddedVector<Widget*>{new Spacer(), new AnimatedImage(animAsset, true), new Spacer()}), 38, 56, 1, true, 4),
        new SizedBox(6, 0),
        new Container(new ScrollColumn(list_items), 76, 64, 0, false, 0)
    }), 128, 64, 0, false, 0);
}

inline Widget* buildIconListMenu(std::initializer_list<MenuItemDef> items) {
    NanoUI::EmbeddedVector<Widget*> list_items;
    for (const auto& item : items) {
        list_items.push_back(new Button(new Container(new Row(NanoUI::EmbeddedVector<Widget*>{
            new SizedBox(8, 0),
            new Image(item.icon, 16, 16),
            new SizedBox(8, 0),
            new Text(item.title, 12),
            new Spacer(),
            new Text(">", 12),
            new SizedBox(8, 0)
        }), 120, 24, 0, true, 4), item.onClick));
        list_items.push_back(new SizedBox(0, 4));
    }
    list_items.push_back(new SizedBox(0, 6));
    list_items.push_back(new Row(NanoUI::EmbeddedVector<Widget*>{new Spacer(), new Button(new Container(new Text(" 返回 ", 12), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }), new Spacer()}));
    return new Container(new ScrollColumn(list_items), 128, 64, 0, false, 0);
}

inline Widget* buildModernMenu(std::initializer_list<MenuItemDef> items, const char* key) {
    NanoUI::EmbeddedVector<Widget*> icons;
    NanoUITitleList titles;
    for (const auto& item : items) {
        icons.push_back(new Button(new Container(new Row(NanoUI::EmbeddedVector<Widget*>{new Spacer(), new Image(item.icon, 16, 16), new Spacer()}), 28, 28, 0, false, 2), item.onClick));
        titles.push_back(item.title);
    }
    return new ModernMenu(icons, titles, NanoUI::makeStateKey(key));
}

// =========================================================
// 传感器文件夹页（当前 256KB 配置下启用）。
// =========================================================
class FolderSensorsApp : public Application {
public:
    const char* getAppName() override { return "FLDR_SNS"; }
    Widget* buildUI() override {
        return buildAnimMenu(asset_my_media, {
            {nullptr, "六轴姿态", []() { AppManager::launch(new SensorApp()); }},
            {nullptr, "激光传感", []() { AppManager::launch(new PATApp()); }},
            {nullptr, "环境气压", []() { AppManager::launch(new BMP280App()); }},
            {nullptr, "电源管理", []() { AppManager::launch(new AXP2101App()); }},
        });
    }
};

class FolderSystemApp : public Application {
public:
    const char* getAppName() override { return "FLDR_SYS"; }
    Widget* buildUI() override {
        return buildIconListMenu({
            {icon_clk, "时间设置", []() { AppManager::launch(new TimeSettingApp()); }},
            {icon_link, "天气校时", []() { AppManager::launch(new WeatherApp()); }},
            {icon_set, "设置", []() { AppManager::launch(new SettingsApp()); }},
            {icon_term, "系统信息", []() { AppManager::launch(new SysInfoApp()); }},
        });
    }
};

class FolderToolsApp : public Application {
public:
    const char* getAppName() override { return "FLDR_DEV"; }
    Widget* buildUI() override {
        return buildIconListMenu({
            {icon_set, "彩灯控制", []() { AppManager::launch(new WS2812ControlApp()); }},
            {icon_term, "固件烧录", []() {
                if (Crabit::CompanionBle::active()) { Popup::showToast("STOP BLE FIRST"); return; }
                AppManager::launch(new FlashApp());
            }},
            // Legacy BLE/Wi-Fi/UART test terminals, source retained.
            // {icon_link, "协处理器", []() {
            //     if (Crabit::CompanionBle::active()) { Popup::showToast("STOP BLE FIRST"); return; }
            //     AppManager::launch(new CoprocLinkApp());
            // }},
        });
    }
};

#if 0
class FolderGamesApp : public Application {
public:
    const char* getAppName() override { return "FLDR_GAM"; }
    Widget* buildUI() override {
        return buildAnimMenu(asset_my_media, {
            {nullptr, "乒乓对战", []() { AppManager::launch(new PongGameApp()); }},
            {nullptr, "趣味涂鸦", []() { AppManager::launch(new EtchGameApp()); }},
        });
    }
};
#endif

// =========================================================
// 主菜单：当前 256KB 配置下的入口集合。
// =========================================================
class LauncherApp : public Application {
public:
    const char* getAppName() override { return "LAUNCHER"; }
    Widget* buildUI() override {
        return buildModernMenu({
            // Diagnostic entry retained for recovery, hidden from normal menus.
            // {icon_link, "BLE LINK", []() { AppManager::launch(new BleLinkApp()); }},
            {icon_link,   "BW16 应用", []() {
                AppManager::launch(new CyberLinkApp());
            }},
            // BLE MODE startup diagnostic is now accessed through the shared
            // Settings/long-UP owner instead of a separate test app.
            {icon_folder_real, "传感器",   []() { AppManager::launch(new FolderSensorsApp()); }},
            {icon_folder_real, "系统设置", []() { AppManager::launch(new FolderSystemApp()); }},
            {icon_term,   "开发工具", []() { AppManager::launch(new FolderToolsApp()); }},
        }, "launcher_main");
    }
};

inline Application* createLauncherApp() { return new LauncherApp(); }
#endif
