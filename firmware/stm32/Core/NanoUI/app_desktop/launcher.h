#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H 

#include "NanoUI/NanoUI.h"
#include "NanoUI/nanoui_assets.h"
#include "dummy_apps.h"

// =========================================================
// 像素图标库 (16x16)
// =========================================================
static const uint8_t icon_clk[]    = { 0x00,0x00,0x07,0xe0,0x08,0x10,0x10,0x08,0x20,0x04,0x20,0x84,0x40,0x82,0x40,0x82,0x40,0x82,0x40,0xc2,0x20,0x04,0x20,0x04,0x10,0x08,0x08,0x10,0x07,0xe0,0x00,0x00 };
static const uint8_t icon_set[]    = { 0x00,0x00,0x06,0x60,0x0f,0xf0,0x1f,0xf8,0x3c,0x3c,0x78,0x1e,0x70,0x0e,0x78,0x1e,0x3c,0x3c,0x1f,0xf8,0x0f,0xf0,0x06,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t icon_gam[]    = { 0x00,0x00,0x0f,0xf0,0x30,0x0c,0x40,0x02,0x48,0x12,0x48,0x12,0x40,0x02,0x48,0x12,0x40,0x02,0x40,0x02,0x40,0x02,0x30,0x0c,0x0f,0xf0,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t icon_snd[]    = { 0x00,0x00,0x01,0x80,0x03,0xc0,0x07,0xe0,0x1f,0xf0,0x1f,0xf8,0x1f,0xfc,0x1f,0xfc,0x1f,0xfc,0x1f,0xf8,0x1f,0xf0,0x07,0xe0,0x03,0xc0,0x01,0x80,0x00,0x00,0x00,0x00 };
static const uint8_t icon_folder[] = { 0x1f,0xf8,0x10,0x08,0x17,0xe8,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x17,0xe8,0x10,0x08,0x1f,0xf8,0x00,0x00,0x00,0x00 };

#include "app_complex_test.h"
#include "app_font_gallery.h"

struct MenuItemDef {
    const uint8_t* icon;
    const char* title;
    VoidCallback onClick;
};

// =========================================================
// 构建二级菜单
// =========================================================
inline Widget* buildAnimMenu(const uint8_t* animAsset, std::initializer_list<MenuItemDef> items) {
    std::vector<Widget*> list_items;
    for (const auto& item : items) {
        list_items.push_back(new Button(new Container(new Row(std::vector<Widget*>{
            new SizedBox(6,0), new Text(item.title, 12), new Spacer(), new Text(">", 12), new SizedBox(6,0)
        }), 76, 22, 0, true, 4), item.onClick));
        list_items.push_back(new SizedBox(0, 4)); 
    }
    list_items.push_back(new SizedBox(0, 4));
    list_items.push_back(new Button(new Container(new Text(" 返回 "), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));

    return new Container(new Row(std::vector<Widget*>{
        new SizedBox(4, 0), 
        new Container(new Column(std::vector<Widget*>{new Spacer(), new AnimatedImage(animAsset, true), new Spacer()}), 38, 56, 1, true, 4),
        new SizedBox(6, 0), 
        new Container(new ScrollColumn(list_items), 76, 64, 0, false, 0)
    }), 128, 64, 0, false, 0);
}

inline Widget* buildIconListMenu(std::initializer_list<MenuItemDef> items) {
    std::vector<Widget*> list_items;
    for (const auto& item : items) {
        list_items.push_back(new Button(new Container(new Row(std::vector<Widget*>{
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
    list_items.push_back(new Row(std::vector<Widget*>{new Spacer(), new Button(new Container(new Text(" 返回主页 "), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }), new Spacer()}));

    return new Container(new ScrollColumn(list_items), 128, 64, 0, false, 0);
}

// =========================================================
// 文件夹：全量功能回归
// =========================================================

class FolderSensorsApp : public Application {
public:
    const char* getAppName() override { return "SENSORS"; }
    Widget* buildUI() override {
        return buildAnimMenu(asset_my_media, {
            {nullptr, "六轴姿态", [](){}}, 
            {nullptr, "光流传感", [](){}}, 
            {nullptr, "电池电量", [](){}},
            {nullptr, "环境地磁", [](){}}
        });
    }
};

class FolderSystemApp : public Application {
public:
    const char* getAppName() override { return "SYSTEM"; }
    Widget* buildUI() override {
        return buildIconListMenu({
            {icon_clk, "时间设置", [](){}}, 
            {icon_set, "屏幕显示", [](){}}, 
            {icon_snd, "声音设置", [](){}},
            {icon_folder, "系统信息", [](){}}
        });
    }
};

class FolderGamesApp : public Application {
public:
    const char* getAppName() override { return "GAMES"; }
    Widget* buildUI() override {
        return buildAnimMenu(asset_my_media, {
            {nullptr, "恐龙快跑", [](){}}, 
            {nullptr, "贪吃蛇",   [](){}}, 
            {nullptr, "乒乓对战", [](){}}
        });
    }
};

class FolderToolsApp : public Application {
public:
    const char* getAppName() override { return "TOOLS"; }
    Widget* buildUI() override {
        return buildIconListMenu({
            {icon_set, "固件烧录", [](){}}, 
            {icon_set, "串口日志", [](){}}, 
            {icon_set, "频率调节", [](){}},
            {icon_set, "总线释放", [](){}},
            {icon_set, "协处理器", [](){}}
        });
    }
};

// =========================================================
// 主启动器
// =========================================================
class LauncherApp : public Application {
public:
    const char* getAppName() override { return "LAUNCHER"; }
    Widget* buildUI() override {
        std::vector<Widget*> main_icons;
        std::vector<std::string> main_titles;
        
        auto add = [&](const uint8_t* icon, const char* title, VoidCallback cb) {
            main_icons.push_back(new Button(new Container(new Row(std::vector<Widget*>{new Spacer(), new Image(icon, 16, 16), new Spacer()}), 28, 28, 0, false, 2), cb));
            main_titles.push_back(title);
        };

        add(icon_folder, "传感器",   []() { AppManager::launch(new FolderSensorsApp()); });
        add(icon_folder, "系统设置", []() { AppManager::launch(new FolderSystemApp()); });
        add(icon_gam,    "经典游戏", []() { AppManager::launch(new FolderGamesApp()); });
        add(icon_set,    "开发工具", []() { AppManager::launch(new FolderToolsApp()); });
        add(icon_set,    "媒体播放", []() { });
        add(icon_set,    "字体演示", []() { AppManager::launch(new FontGalleryApp()); });

        return new ModernMenu(main_icons, main_titles, "main_launcher_sandbox");
    }
};

inline Application* createLauncherApp() { return new LauncherApp(); }

#endif
