# NanoUI：从一个小 App 看框架边界

NanoUI 是一套面向小屏设备的 C++ UI 框架。你用 `Widget` 描述界面，平台
提供画布、输入、时钟和字体实现。这样，同一份 App 代码可以被不同端的
适配层使用；但这不等于“任何设备拿来就能运行”，适配层仍要接上真实驱动、
构建系统和设备生命周期。

本文对应仓库源码快照 `549f3a3`。它介绍的是可检查的框架源码和示例，不是
稳定固件版本声明。当前默认 BW16 产品仍处在缩减 App 载荷的实验状态；这里
的架构说明、桌面预览或编译结果都不能代替手表上的验收。

## 先跑一个最小界面

新 App 从公共头文件 `NanoUI.h` 开始。下面的类按 FirstApp 实际使用的 API
构造一个标题和开关：

```cpp
#include <NanoUI.h>

class HelloApp : public Application {
public:
    HelloApp() : enabled(false) {}
    const char* getAppName() override { return "Hello"; }

    Widget* buildUI() override {
        return new Column({
            new Text("Hello, NanoUI", 12),
            new Row({new Text("Enabled", 12), new Switch(enabled)})
        });
    }

private:
    bool enabled;
};
```

`buildUI()` 返回新的 Widget 树；`Column` 和 `Row` 组合子项，`Text` 显示文本，
`Switch` 直接绑定 App 的布尔状态。这里沿用原框架的动态对象组合方式：
Widget/RenderObject 会在运行时创建，不要把它误读成静态分配 UI。传给控件的
状态和文本也必须在对应 RenderObject 使用期间保持有效。

这个类还不是一个完整程序。平台代码需要建立 `Canvas`、`InputDriver` 和时钟，
创建 `WindowManager`，把 App 生成的树连同 owner 一起 push 进去，并在平台循环
中调用更新和绘制。App 的寿命必须覆盖使用它的 RenderObject。最完整、最小的
实物参考是 [`FirstApp`](../../libraries/NanoUI/examples/FirstApp/README.md)：
同一 App 由 Windows GDI 预览和 Arduino 串口文字端口消费。后者是教学用的
ASCII 画布，不是 OLED 驱动。

## 框架、App、端口各自负责什么

```text
App（界面树、状态、产品行为、自己的资源）
                 ↓
NanoUI（Widget、RenderObject、布局、导航和控件）
                 ↓
平台端口（Canvas、InputDriver、时钟、字体/内存适配）
                 ↓
板级驱动与 SDK（显示、按键、RTOS、芯片服务）
```

公共入口 [`libraries/NanoUI/src/NanoUI.h`](../../libraries/NanoUI/src/NanoUI.h)
只聚合框架接口，不包含 Arduino、STM32 HAL 或产品服务。产品 App 要传感器、
无线或电源服务时，应由 App/端口显式接入，而不是让可复用核心偷偷依赖某块板。
Crabit 中 STM32 与 BW16 的接入和 SDK/build 各自独立；它们共享框架源码不代表
可以共用一套构建命令或 SDK 状态。

维护中的原始框架位于 `libraries/NanoUI/src/NanoUI/product/`。仓库还保存了
`experimental/` 下的另一套实验实现，但它不属于默认产品核心。既有产品的旧
include 路径通过兼容转发维持；新代码优先使用 `<NanoUI.h>`，无需为了采用新
入口而批量改写旧 App。详细约束见
公共入口政策。

## 本机先验，不要误判成设备验收

FirstApp 的 Windows 项目位于
[`libraries/NanoUI/examples/FirstApp`](../../libraries/NanoUI/examples/FirstApp/README.md)。
在该目录运行：

```powershell
cmake -S . -B build
cmake --build build --config Release
```

它用 Win32 GDI 呈现 128×64 画布，不依赖产品模拟器的 SDL/Tracy。示例 CMake
默认从库内相对路径加入 NanoUI；独立导出时可通过
`-DNANOUI_LIBRARY_DIR=C:/path/to/NanoUI` 指向库目录。若需无窗口渲染，运行
`NanoUIFirstApp.exe --headless [output.bmp]`；这只是 host 渲染检查。

产品路径不是这个命令的延伸：STM32 的 NanoUI 子目录通过 CMake 接入共享核心，
再由 STM32/HAL/FreeRTOS 产品提供板级能力；BW16 sketch 从
[`NanoUI.ino`](../../firmware/rtl8720dn/NanoUI/NanoUI.ino) 进入
[`main_entry.cpp`](../../firmware/rtl8720dn/NanoUI/src/main_entry.cpp)，在产品树
中使用独立 Arduino/AmebaD 适配。两端 SDK 与 build 各自独立。只有逐个目标实际
构建并验证后，才可报告该目标通过。CubeMX/Keil 的接入说明是移植配方，不是
原生项目验证结果。当前快照没有已实现并验证的 Zephyr 端口，因此不能把“未来
可添加端口”写成“已支持 Zephyr”。

## 继续探索

- [资源与字体/图片生成](assets.md)：扫描范围、字体覆盖、生成物归属与安全重建。
- [NanoUI 库 README](../../libraries/NanoUI/README.md)：独立构建、API 边界和测试入口。
- [FirstApp 源码](../../libraries/NanoUI/examples/FirstApp/src/apps/hello_app.h)：最小 App 的实际定义。
- [平台接入说明](../../libraries/NanoUI/integrations/README.md)：移植时 App、端口和生成代码的边界。
- [仓库概览](../README.md)、[新手入口](getting-started.md)、[构建与烧录](build-and-flash.md)、[当前状态](status.md)及[移动端](mobile.md)文档分别覆盖仓库范围；固件/烧录状态不属于 NanoUI API。
