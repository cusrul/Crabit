#ifndef APP_LAUNCHER_H
#define APP_LAUNCHER_H

#include "../src/NanoUI.h"
#include "../src/main_entry.h"
#include "../src/CrabitBuildProfile.h"
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
#include "../src/network_ui/strings.h"
#endif
#include "../config.h"
#include "../RadioSvc.h"
#include <stdio.h>
#include <string.h>

#if !CRABIT_BW16_DIAG_SMALL_APP_SET || !CRABIT_BW16_IS_PRODUCT
#include "../src/nanoui_assets.h"
// 子 App 包含
#if CRABIT_BW16_IS_BLE_MOUSE_LAB || CRABIT_BW16_IS_ALL_LAB
#include "app_ble_mouse.h"
#endif
#if CRABIT_BW16_IS_BLE_PPT_LAB || CRABIT_BW16_IS_ALL_LAB
#include "app_ble_ppt.h"
#endif
#if CRABIT_BW16_IS_BLE_KBD_LAB || CRABIT_BW16_IS_ALL_LAB
#include "app_ble_kbd_test.h"
#endif

#if CRABIT_BW16_ENABLE_GAMES || CRABIT_BW16_NETWORK_GAME
#include "wifi_hack_core.h"
#if CRABIT_BW16_ENABLE_GAMES
#include "castle_boy_app.h"
#endif
#if CRABIT_BW16_ENABLE_GAMES || (CRABIT_BW16_NETWORK_GAME & 1)
#include "app_arduboy3d.h"
#endif
#if CRABIT_BW16_ENABLE_GAMES || (CRABIT_BW16_NETWORK_GAME & 2)
#include "app_hopper.h"
#endif
#endif
#if CRABIT_BW16_ENABLE_WIFI_LAB
#include "app_wifi_tools.h"
#endif
#if CRABIT_BW16_ENABLE_BLE_SPAM_APP
#include "app_ble_spam.h"
#endif

#include "../Protocol.h"
#if CRABIT_BW16_ENABLE_OTA
#include <OTA.h>
#include <AmebaMDNS.h>
#endif
#include <WiFi.h>
#endif

extern "C" void sys_reset(void); 

using namespace NanoUI;

#if 0 // 只有说明文字的占位 App，暂不编译。
class ProductNoticeApp : public Application {
public:
    const char* getAppName() override { return "功能状态"; }

    Widget* buildUI() override {
        return new Container(new Column(std::vector<Widget*>{
            new Spacer(),
            new Text("无线控制", 8),
            new Text("应用与串口控制", 8),
            new Spacer(),
            new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2),
                       [](){ AppManager::exitCurrentApp(); })
        }), 128, 64, 0, false, 0);
    }
};
#endif

class PairDiagApp : public Application {
private:
    static constexpr uint8_t LINE_HEIGHT = 8;
    static constexpr uint8_t BUTTON_HEIGHT = 12;
    static constexpr uint8_t VERTICAL_GAP = 2;
    static constexpr uint8_t CONTENT_HEIGHT =
        LINE_HEIGHT * 4 + VERTICAL_GAP + BUTTON_HEIGHT;
    static_assert(CONTENT_HEIGHT <= 64, "PAIR DIAG must fit the 64px OLED");

    char radio_line[40] = "无线:空闲";
    char pair_line[40] = "配对:空闲 0/37";
    char store_line[24] = "存储:无";

    static const char* pairStateLabel(RadioSvc::PairDiagnosticState state) {
        switch (state) {
            case RadioSvc::PAIR_DIAG_IDLE: return "空闲";
            case RadioSvc::PAIR_DIAG_WAITING: return "等待";
            case RadioSvc::PAIR_DIAG_RAM_CONFIRMED: return "内存确认";
            case RadioSvc::PAIR_DIAG_CANCELLED: return "取消";
            case RadioSvc::PAIR_DIAG_TIMEOUT: return "超时";
            default: return "关闭";
        }
    }

    static const char* radioStateLabel(RadioSvc::State state) {
        switch (state) {
            case RadioSvc::STARTING: return "启动";
            case RadioSvc::ADVERTISING: return "广播";
            case RadioSvc::CONNECTED: return "连接";
            case RadioSvc::STOPPING: return "停止";
            case RadioSvc::FAILED: return "失败";
            case RadioSvc::TIMEOUT: return "超时";
            default: return "空闲";
        }
    }

    void refresh() {
        const RadioSvc::Snapshot snapshot = RadioSvc::snapshot();
        char next_radio[sizeof(radio_line)];
        char next_pair[sizeof(pair_line)];
        snprintf(next_radio, sizeof(next_radio), "无线:%s 连接%u",
                 radioStateLabel(snapshot.state), snapshot.conn_id);
        snprintf(next_pair, sizeof(next_pair), "配对:%s %u/37",
                 pairStateLabel(snapshot.pair_diagnostic_state),
                 snapshot.pair_rx_length);
        if (strcmp(radio_line, next_radio) == 0 && strcmp(pair_line, next_pair) == 0) return;
        strncpy(radio_line, next_radio, sizeof(radio_line) - 1);
        radio_line[sizeof(radio_line) - 1] = '\0';
        strncpy(pair_line, next_pair, sizeof(pair_line) - 1);
        pair_line[sizeof(pair_line) - 1] = '\0';
        if (WindowManager::instance) WindowManager::instance->markDirty();
    }

public:
    const char* getAppName() override { return "配对诊断"; }

    void onCreate() override {
        RadioSvc::beginPairDiagnostic();
        RadioSvc::requestStart();
    }

    void onDestroy() override {
        RadioSvc::endPairDiagnostic();
    }

    void onTick() override { refresh(); }

    Widget* buildUI() override {
        return new Container(new Column(std::vector<Widget*>{
            new Text("配对诊断", LINE_HEIGHT),
            new Text(radio_line, LINE_HEIGHT),
            new Text(pair_line, LINE_HEIGHT),
            new Text(store_line, LINE_HEIGHT),
            new SizedBox(0, VERTICAL_GAP),
            new Row(std::vector<Widget*>{
                new Button(new Container(new Text(" 内存确认 ", LINE_HEIGHT), 52, BUTTON_HEIGHT, 0, true, 2), []() {
                    RadioSvc::confirmPairInRam();
                }),
                new Spacer(),
                new Button(new Container(new Text(" 取消 ", LINE_HEIGHT), 58, BUTTON_HEIGHT, 0, true, 2), []() {
                    RadioSvc::cancelPairDiagnostic();
                })
            })
        }), 128, 64, 0, false, 0);
    }
};

#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
// Restore the normal menu without linking its large animation or lab payloads.
#define CRABIT_MENU_ART nullptr
#else
#define CRABIT_MENU_ART asset_my_media
#endif
// ==========================================
// 系统图标库 (16x16)
// ==========================================
static const uint8_t icon_hacker_main[] = { 0x1f, 0xf8, 0x10, 0x08, 0x10, 0x08, 0x12, 0x48, 0x11, 0x28, 0x10, 0x98, 0x10, 0x08, 0x10, 0x08, 0x13, 0xc8, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x10, 0x08, 0x1f, 0xf8, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t icon_game_main[]   = { 0x1f, 0xf8, 0x20, 0x04, 0x40, 0x02, 0x4f, 0xf2, 0x48, 0x12, 0x48, 0x12, 0x4f, 0xf2, 0x40, 0x02, 0x42, 0x82, 0x47, 0xc2, 0x42, 0xa2, 0x40, 0x02, 0x20, 0x04, 0x1f, 0xf8, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t icon_folder_main[] = { 0x1f, 0xf8, 0x10, 0x08, 0x17, 0xe8, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x17, 0xe8, 0x10, 0x08, 0x1f, 0xf8, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t icon_ble_main[]    = { 0x01, 0x00, 0x01, 0x80, 0x01, 0x40, 0x01, 0x20, 0x11, 0x10, 0x09, 0x20, 0x05, 0x40, 0x03, 0x80, 0x03, 0x80, 0x05, 0x40, 0x09, 0x20, 0x11, 0x10, 0x01, 0x20, 0x01, 0x40, 0x01, 0x80, 0x01, 0x00 };

// ==========================================
// Modern 风格二级菜单 (Anim + List)
// ==========================================
// Text borrows its content. Fixed menu labels must outlive the returned tree;
// never pass c_str() from a temporary string container here.
inline Widget* buildModernAnimMenu(const uint8_t* animAsset, std::vector<const char*> titles, std::vector<VoidCallback> callbacks) {
    std::vector<Widget*> list_items;
    for (size_t i = 0; i < titles.size(); i++) {
        list_items.push_back(new Button(new Container(new Row(std::vector<Widget*>{
            new SizedBox(6,0), new Text(titles[i], 8), new Spacer(), new Text(">", 8), new SizedBox(6,0)
        }), 76, 22, 0, true, 4), callbacks[i]));
        list_items.push_back(new SizedBox(0, 4));
    }
    list_items.push_back(new SizedBox(0, 4));
    list_items.push_back(new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));

    return new Container(new Row(std::vector<Widget*>{
        new SizedBox(4, 0),
        new Container(new Column(std::vector<Widget*>{new Spacer(),
#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
            new Image(icon_folder_main, 16, 16),
#else
            new AnimatedImage(animAsset, true),
#endif
            new Spacer()}), 38, 56, 1, true, 4),
        new SizedBox(6, 0),
        new Container(new ScrollColumn(list_items), 76, 64, 0, false, 0)
    }), 128, 64, 0, false, 0);
}

#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
// Honest capability pages: no hidden SDK calls or diagnostics behind a folder.
inline Widget* buildProductInfoPage(const char* title, const char* line) {
    return new Container(new Column(std::vector<Widget*>{
        new Text(title, 8),
        new Spacer(),
        new Text(line, 8),
        new Spacer(),
        new Button(new Container(new Text(" 返回 ", 8), 0, 14, 0, true, 2),
                   [](){ AppManager::exitCurrentApp(); })
    }), 128, 64, 0, false, 0);
}
#endif

// ==========================================
// 文件夹定义 (二级菜单)
// ==========================================

class BleFolderApp : public Application {
public:
    const char* getAppName() override { return "蓝牙中心"; }
    Widget* buildUI() override {
#if CRABIT_BW16_IS_ALL_LAB
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"鼠标", "翻页", "键盘"},
            std::vector<VoidCallback>{
                [](){ AppManager::launch(new AppBleMouse()); },
                [](){ AppManager::launch(new AppBlePPT()); },
                [](){ AppManager::launch(new AppBleKbdTest()); }
            });
#elif CRABIT_BW16_IS_BLE_MOUSE_LAB
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"鼠标"},
            std::vector<VoidCallback>{
                [](){ AppManager::launch(new AppBleMouse()); }
            });
#elif CRABIT_BW16_IS_BLE_PPT_LAB
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"翻页"},
            std::vector<VoidCallback>{
                [](){ AppManager::launch(new AppBlePPT()); }
            });
#elif CRABIT_BW16_IS_BLE_KBD_LAB
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"键盘"},
            std::vector<VoidCallback>{
                [](){ AppManager::launch(new AppBleKbdTest()); }
            });
#elif CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_WIFI_TOOLS_LAB
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"蓝牙广播"},
            std::vector<VoidCallback>{[](){ AppManager::launch(new AppBleSpam()); }});
#elif CRABIT_BW16_PROFILE == CRABIT_BW16_PROFILE_NETWORK_OTA_LAB
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"返回"},
            std::vector<VoidCallback>{[](){ AppManager::exitCurrentApp(); }});
#else
#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
        return buildProductInfoPage("蓝牙", "请使用主控设置");
#else
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"配对诊断"},
            std::vector<VoidCallback>{
                [](){ AppManager::launch(new PairDiagApp()); }
            });
#endif
#endif
    }
};

class GamesFolderApp : public Application {
public:
    const char* getAppName() override { return "游戏娱乐"; }
    Widget* buildUI() override {
#if CRABIT_BW16_NETWORK_GAME
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{
#if CRABIT_BW16_NETWORK_GAME & 1
                "立体迷宫",
#endif
#if CRABIT_BW16_NETWORK_GAME & 2
                "立体跳跃",
#endif
            },
            std::vector<VoidCallback>{
#if CRABIT_BW16_NETWORK_GAME & 1
                [](){ AppManager::launch(new Arduboy3DApp()); },
#endif
#if CRABIT_BW16_NETWORK_GAME & 2
                [](){ AppManager::launch(new AppHopper()); }
#endif
            });
#elif CRABIT_BW16_ENABLE_GAMES
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"城堡冒险", "立体迷宫", "立体跳跃"},
            std::vector<VoidCallback>{
                [](){ AppManager::launch(new CastleBoyApp()); },
                [](){ AppManager::launch(new Arduboy3DApp()); },
                [](){ AppManager::launch(new AppHopper()); }
            });
#else
#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
        return buildProductInfoPage("游戏", "当前未启用");
#else
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"返回"},
            std::vector<VoidCallback>{
                [](){ AppManager::exitCurrentApp(); }
            });
#endif
#endif
    }
};

#if !CRABIT_BW16_ENABLE_WIFI_LAB
class WifiToolsMenuApp : public Application {
public:
    const char* getAppName() override { return "网络工具"; }
    Widget* buildUI() override {
#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
        return buildProductInfoPage("网络工具", "当前未启用");
#else
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"返回"},
            std::vector<VoidCallback>{
                [](){ AppManager::exitCurrentApp(); }
            });
#endif
    }
};
#endif

class SystemFolderApp : public Application {
public:
    const char* getAppName() override { return "系统设置"; }
    Widget* buildUI() override {
#if CRABIT_BW16_DIAG_SMALL_APP_SET && CRABIT_BW16_IS_PRODUCT
        return buildProductInfoPage("CRABIT", "BW16 / NanoUI");
#elif CRABIT_BW16_ENABLE_OTA
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"无线更新"},
            std::vector<VoidCallback>{
                [](){ current_mode = MODE_OTA; }
            });
#else
        return buildModernAnimMenu(CRABIT_MENU_ART,
            std::vector<const char*>{"返回"},
            std::vector<VoidCallback>{[](){ AppManager::exitCurrentApp(); }});
#endif
    }
};

// ==========================================
// 一级菜单 (ModernMenu)
// ==========================================
class LauncherApp : public Application {
public:
    const char* getAppName() override { return "应用菜单"; }
    
    Widget* buildUI() override {
        std::vector<Widget*> icons;
        std::vector<std::string> titles;

        // 1. 蓝牙
#if CRABIT_BW16_ENABLE_BLE_SPAM_APP || CRABIT_BW16_IS_ALL_LAB || CRABIT_BW16_IS_BLE_MOUSE_LAB || CRABIT_BW16_IS_BLE_PPT_LAB || CRABIT_BW16_IS_BLE_KBD_LAB
        icons.push_back(new Button(new Container(new Row(std::vector<Widget*>{new Spacer(), new Image(icon_ble_main, 16, 16), new Spacer()}), 28, 28, 0, false, 2), [](){ AppManager::launch(new BleFolderApp()); }));
        titles.push_back("蓝牙中心");
#endif

        // 2. 黑客
#if CRABIT_BW16_ENABLE_WIFI_LAB
        icons.push_back(new Button(new Container(new Row(std::vector<Widget*>{new Spacer(), new Image(icon_hacker_main, 16, 16), new Spacer()}), 28, 28, 0, false, 2), [](){ AppManager::launch(new WifiToolsMenuApp()); }));
        titles.push_back("网络审计");
#endif

        // 3. 游戏
#if CRABIT_BW16_ENABLE_GAMES || CRABIT_BW16_NETWORK_GAME
        icons.push_back(new Button(new Container(new Row(std::vector<Widget*>{new Spacer(), new Image(icon_game_main, 16, 16), new Spacer()}), 28, 28, 0, false, 2), [](){ AppManager::launch(new GamesFolderApp()); }));
        titles.push_back("游戏娱乐");
#endif

        // 4. 系统
#if CRABIT_BW16_ENABLE_OTA
        icons.push_back(new Button(new Container(new Row(std::vector<Widget*>{new Spacer(), new Image(icon_folder_main, 16, 16), new Spacer()}), 28, 28, 0, false, 2), [](){ AppManager::launch(new SystemFolderApp()); }));
        titles.push_back("系统设置");
#endif

        if (icons.empty()) {
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
            return new Container(new Column(std::vector<Widget*>{
                new Spacer(), new Text(NetworkTrialUi::Title, 8),
                new Text(NetworkTrialUi::Hint, 8), new Spacer(),
                new Button(new Text(NetworkTrialUi::Back, 8), [](){ AppManager::exitCurrentApp(); })
            }), 128, 64);
#else
            return new Container(new Column(std::vector<Widget*>{
                new Spacer(), new Text("暂无应用", 8), new Spacer(),
                new Button(new Text("返回", 8), [](){ AppManager::exitCurrentApp(); })
            }), 128, 64);
#endif
        }

        return new PageTransition(
            new ModernMenu(icons, titles, "launcher_main"),
            TransitionType::FadeOutIn
        );
    }
};

#undef CRABIT_MENU_ART

inline Application* createLauncherApp() { return new LauncherApp(); }

#endif
