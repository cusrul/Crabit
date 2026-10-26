// =========================================================
// nano_ui.ino - 主程序入口
// =========================================================

// 1. 解决 Realtek SDK 的 SPI 宏冲突 (必须在最前面)
#include "wiring.h"
#include <SPI.h>
#ifdef SPI_MODE0
#undef SPI_MODE0
#endif
#define SPI_MODE0 0x00

// 2. 引入配置
#include "config.h"
#include "../src/NanoUI.h"
// // 3. 引入 HAL (硬件抽象层) 和 驱动
// #include "hal/canvas.h"
// #include "hal/input.h"
// #include "drivers/ssd1306_adapter.h"
// #include "drivers/button_driver.h"

// // 4. 引入 UI 核心框架
// #include "core/window_manager.h"

// // 5. 引入所有 Widget 组件
// #include "widgets/flex.h"
// #include "widgets/text.h"
// #include "widgets/basic.h"
// #include "widgets/container.h"
// #include "widgets/button.h"
// #include "widgets/carousel.h"
// #include "widgets/image.h"
// #include "widgets/controls.h"

// =========================================================
// 全局图标资源 (保留原样，未修改)
// =========================================================
static const uint8_t icon_home[] PROGMEM = {0x00,0x00,0x01,0x80,0x03,0xc0,0x07,0xe0,0x0f,0xf0,0x1f,0xf8,0x1f,0xf8,0x30,0x0c,0x3f,0xfc,0x3f,0xfc,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x30,0x0c,0x00,0x00,0x00,0x00};
static const uint8_t icon_wifi[] PROGMEM = {0x00,0x00,0x00,0x00,0x0f,0xf0,0x3f,0xfc,0x70,0x0e,0xe0,0x07,0xc1,0x83,0x87,0xe1,0x0f,0xf0,0x18,0x18,0x00,0x00,0x03,0xc0,0x03,0xc0,0x00,0x00,0x00,0x00,0x00,0x00};
static const uint8_t icon_chip[] PROGMEM = {0x00,0x00,0x3f,0xfc,0x20,0x04,0x2a,0xa4,0x20,0x04,0x2a,0xa4,0x20,0x04,0x20,0x04,0x2f,0xf4,0x20,0x04,0x20,0x04,0x2f,0xf4,0x20,0x04,0x3f,0xfc,0x00,0x00,0x00,0x00};
static const uint8_t icon_gear[] PROGMEM = {0x03,0xc0,0x04,0x20,0x09,0x90,0x11,0x88,0x23,0xc4,0x44,0x22,0x48,0x12,0x80,0x01,0x80,0x01,0x48,0x12,0x44,0x22,0x23,0xc4,0x11,0x88,0x09,0x90,0x04,0x20,0x03,0xc0};

// 系统模拟数据
int sys_battery = 84;
bool wifi_enabled = true;
bool isWifiOn = true;
bool isDarkMode = false;
bool isAutoUpdate = true;
float brightness = 0.8;

// =========================================================
// 驱动实例化 & 依赖注入
// =========================================================

// A. 显示驱动链
// 1. 实例化底层的 Adafruit 驱动
Adafruit_SSD1306 rawDisplay(128, 64, OLED_MOSI, OLED_CLK, OLED_DC, OLED_RST, OLED_CS);

// 2. 实例化适配器 (它把 rawDisplay 包装成了抽象的 Canvas)
SSD1306Adapter displayAdapter(&rawDisplay);

// B. 输入驱动链
// 1. 实例化按键驱动 (它实现了抽象的 InputDriver)
// 注意：这里使用了 config.h 中的引脚定义 (BTN_UP_PIN 等)
ButtonDriver inputDriver(BTN_UP_PIN, BTN_BACK_PIN, BTN_OK_PIN);

// C. UI 核心
// 将“显示适配器”和“输入驱动”注入到 WindowManager
WindowManager window(&displayAdapter, &inputDriver);

// =========================================================
// 辅助组件构建器 (UI Helpers)
// =========================================================

Widget* BackButton() {
    return new Row({
        new Spacer(), 
        new Button(
            new Container(new Text(" < BACK "), 0, 0, 2, true, 4), 
            [](){ Navigator::pop(); }
        ),
        new Spacer()
    });
}

Widget* MenuItem(const uint8_t* icon, const char* label, VoidCallback cb) {
    return new Button(
        new Column({
            new Image(icon, 16, 16),
            new SizedBox(0, 4),
            new Text(label)
        }), cb
    );
}

Widget* WiFiItem(const char* ssid, const char* rssi) {
    return new Row({
        new Button(
            new Container(
                new Row({ new Text(ssid), new SizedBox(6,0), new Text(rssi, 1) }), 
                0, 0, 2, false
            ), 
            [](){ /* 连接逻辑 */ }
        ),
        new Spacer() 
    });
}

// =========================================================
// 页面构建 (View Builders)
// =========================================================

Widget* buildDashboard();
Widget* buildSettings();
Widget* buildWiFiPage();

// --- [A. 表盘主页] ---
Widget* buildWatchFace() {
    return new Container(
        new Column({
            // 顶部状态栏
            new Row({
                new Image(icon_wifi, 16, 16),
                new SizedBox(4, 0),
                new Text(wifi_enabled ? "CONNECTED" : "OFFLINE"),
                new Spacer(), 
                new Text("84%"),
                new SizedBox(2, 0),
                new Container(new SizedBox(10, 4), 0,0, true) // 电池图标
            }),
            
            new Spacer(), // 弹簧

            // 时间显示
            new Row({
                new Spacer(), new Text("10", 2), new Text(":", 2), new Text("30", 2), new Spacer()
            }),
            new SizedBox(0, 4),
            new Row({ new Spacer(), new Text("WED, FEB 12"), new Spacer() }),

            new Spacer(), // 弹簧

            // 底部菜单按钮
            new Button(
                new Container(new Text(" MENU "), 8, 8, 3, true, 4),
                [](){ Navigator::push(buildDashboard()); }
            ),
            new SizedBox(0, 2)
        }), 
        0, 0, 2, false // 全屏无边框
    );
}

// --- [B. 仪表盘] ---
Widget* buildDashboard() {
    return new Container(
        new Fill(
            new Column({
                new Text(" - MENU - "),
                new Spacer(1), 
                new Carousel({
                    MenuItem(icon_home, "HOME", [](){ Navigator::pop(); }),
                    MenuItem(icon_wifi, "WIFI", [](){ Navigator::push(buildWiFiPage()); }),
                    MenuItem(icon_chip, "SYS",  [](){ /* TODO */ }),
                    MenuItem(icon_gear, "SET",  [](){ Navigator::push(buildSettings()); }),
                }, 18), 
                new Spacer(1),
                new Text("CyberWatch OS 1.0", 1, true)
            })
        ), 
        0, 0, 4, true, 8 
    );
}

// --- [C. WiFi] ---
Widget* buildWiFiPage() {
    return new Container(
        new ScrollColumn({
            new Row({ new Text("NETWORKS (3)"), new Spacer() }),
            new SizedBox(0, 6),
            WiFiItem("1. Home_5G", "-45"), new SizedBox(0, 4),
            WiFiItem("2. Office_Guest", "-60"), new SizedBox(0, 4),
            WiFiItem("3. Free_WiFi", "-88"), new SizedBox(0, 4),
            
            // 演示长文本滚动
            new Row({
               new Button(
                   new Container(new Text("4. A_Very_Long_SSID_Name_Scrolls", 1, true), 0, 0, 2, false), 
                   [](){}
               ),
               new Spacer()
            }),
            new Spacer(), 
            BackButton()
        }), 0, 0, 4, true, 4
    );
}

// --- [D. 设置页] ---
Widget* buildSettings() {
    return new Container(
        new Fill( 
            new ScrollColumn({
                // 1. 系统状态卡片
                new Button(
                    new Container(
                        new Column({
                            new Row({new Text(" SYSTEM STATUS "), new Spacer()}),
                            new SizedBox(0, 4),
                            new Row({new Text("CPU"), new Spacer(), new Text("32%")}),
                            new ProgressBar(0.32, 100), 
                            new SizedBox(0, 4),
                            new Row({new Text("RAM"), new Spacer(), new Text("78%")}),
                            new ProgressBar(0.78, 100)
                        }),
                        0, 0, 2, false
                    ),
                    []() {}
                ),
                new SizedBox(0, 6),
                new Container(new SizedBox(100, 1), 0, 0, 0, true), // 分割线
                new SizedBox(0, 6),

                // 2. 亮度调节
                new Row({new Text("Brightness"), new Spacer()}),
                new SizedBox(0, 2),
                new Row({
                    new Text("Bright"), new Spacer(),
                    new Text("-"), new SizedBox(4, 0),
                    new Slider(brightness, 60, [](float v) { brightness = v; }),
                    new SizedBox(4, 0), new Text("+")
                }),
                new SizedBox(0, 8),

                // 3. 开关项
                new Row({ new Text("WiFi Radio"), new Spacer(), new Switch(isWifiOn, [](bool v) { isWifiOn = v; }) }),
                new SizedBox(0, 6),
                new Row({ new Text("Dark Mode"), new Spacer(), new Switch(isDarkMode, [](bool v) { isDarkMode = v; }) }),
                new SizedBox(0, 6),

                // 4. 退出
                new Row({ new Text("Exit"), new Spacer(), BackButton() })
            })
        ),
        0, 0, 4, true, 4
    ); 
}

// =========================================================
// 6. Arduino 主循环
// =========================================================

void setup() {
    Serial.begin(115200);
    
    // 1. 初始化底层硬件 (Raw Hardware)
    // 必须先初始化 Adafruit 库，否则 Adapter 调用会崩溃
    if(!rawDisplay.begin(SSD1306_SWITCHCAPVCC)) {
        Serial.println(F("OLED Init Failed"));
        for(;;);
    }
    
    // 2. 初始化输入驱动
    inputDriver.init();
    
    // 3. 初始化 UI 导航器
    Navigator::init(&window);
    
    // 4. 加载首页
    Navigator::push(buildWatchFace());
}

void loop() {
    // 1. 核心循环：处理输入 -> 分发事件 -> 更新动画
    window.update();
    window.markDirty();
    // 2. 渲染循环：如果 UI 有变化 (Dirty)，执行绘制
    window.drawFrame();
    
    // 移除 delay，保证输入响应最灵敏
}