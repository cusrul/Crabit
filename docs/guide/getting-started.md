# 快速开始

Crabit 可以分开体验：只运行 UI、只运行客户端，或者编译实体设备固件。
第一次不必把所有工具链装齐。先读[当前状态](status.md)，确认你拿到的是哪一个源码版本。

仓库尚未指定公开克隆地址。取得源码后，从含有 `README.md`、`libraries/`、`firmware/` 的仓库根目录打开终端；下文不依赖维护者的个人目录。

## 路线一：没有手表，先跑 NanoUI

需要 Windows、CMake 3.20+ 和可用的 C++ 编译器。可使用已有的 Visual Studio C++ 工具链，或者带 Ninja 的 MinGW；不要在同一个构建目录切换编译器。

在仓库根目录运行：

```powershell
cmake -S libraries/NanoUI/examples/FirstApp -B build/first-app
cmake --build build/first-app --config Release
ctest --test-dir build/first-app -C Release --output-on-failure
```

运行生成的 `NanoUIFirstApp.exe`。Visual Studio 通常放在 `build/first-app/Release/`，Ninja 通常直接放在 `build/first-app/`。用资源管理器进入对应目录打开即可。

你会看到 128×64 画布上的示例界面。输入操作见[FirstApp 说明](../../libraries/NanoUI/examples/FirstApp/README.md)。
它不会访问手表、启动无线或改写固件。若只需输出图像，在可执行文件目录运行：

```powershell
.\NanoUIFirstApp.exe --headless preview.bmp
```

下一步修改示例里的[Hello App](../../libraries/NanoUI/examples/FirstApp/src/apps/hello_app.h)，再看[NanoUI 指南](nanoui.md)。中文显示为方框时，请检查[字体覆盖](assets.md)，而不是更换框架。

## 路线二：先看 Flutter 客户端和 3D 手表

需要 Flutter SDK、其要求的 Dart 版本和 Windows 桌面 C++ 工具链。已配置环境时：

```powershell
cd apps/mobile
flutter pub get
flutter run -d windows
```

设备模型和本地 NanoUI 预览不要求连接真实手表，也不是实体 OLED 的远程镜像。
没有硬件时先使用本地页面，不发起 BLE 连接。进一步说明见[客户端指南](mobile.md)。

Android/iOS 等目录存在，但不意味着本次版本完成了这些平台的构建、权限或 BLE 验证。

## 路线三：已经有硬件

先确认[硬件版本、Flash 容量和接线](hardware.md)，然后阅读[构建与烧录](build-and-flash.md)。
两颗芯片各自编译、各自产物；STM32 ELF 不能用于 BW16，Arduino sketch 也不是 STM32 工程。

当前候选用于排查无线问题，并不是推荐新用户直接烧录的稳定发行版。
烧录前应保存已知可用的 STM32/BW16 成对版本、构建参数和文件校验值。不能只凭目录里的“最新文件”选择固件。

## 想确认开发环境

回到仓库根目录：

```powershell
python tools/engineering.py check
python tools/engineering.py doctor
```

前者验证仓库工程配置；后者探测本机已有工具，不安装 SDK。
某个工具缺失或版本探测超时，只影响对应路线，不妨碍阅读源码或运行其他独立组件。

如果愿意参与开发，从[贡献指南](../../CONTRIBUTING.md)开始。无需为了贡献一篇文档先搭建全套硬件环境。
