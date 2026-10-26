# 构建、产物与烧录

本文命令默认在仓库根目录执行，除非代码块先切换目录。这里只给出构建与检查流程；
烧录、复位、进入下载模式和供电由设备操作者执行。没有选定稳定固件组合前，不提供一键刷写脚本。

## 1. 先选源码和环境

记录 `git rev-parse HEAD`、板版本、实际 MCU 容量和工具版本。
当前用户验收与普通构建对应关系见工程收尾交接，不是稳定发行版。
默认 BW16 含实验诊断开关，详见[状态](status.md)。

| 目标 | 前置工具 | 不需要 |
| --- | --- | --- |
| NanoUI Windows | CMake 3.20+、Windows C++ 编译器；Ninja 或 Visual Studio 生成器 | STM32/BW16 SDK、实体手表 |
| STM32 | CMake、Ninja、PATH 中可用的 `arm-none-eabi-gcc/g++` | Arduino SDK |
| BW16 | Arduino IDE 或 CLI、Realtek AmebaD core、Ai-Thinker BW16 板配置 | STM32 工具链 |
| Flutter Windows | Flutter/Dart、Visual Studio 桌面 C++ 工具链 | 手表烧录器 |

工具发现可用 `python tools/engineering.py doctor`。它不安装或改写工具链。
新机器的 SDK 来源、版本和许可应按所选发行版确认；本仓库没有把本机安装环境打包成可复现的 SDK 镜像。

## 2. NanoUI 桌面示例和框架测试

示例：

```powershell
cmake -S libraries/NanoUI/examples/FirstApp -B build/first-app
cmake --build build/first-app --config Release
ctest --test-dir build/first-app -C Release --output-on-failure
```

框架测试独立于示例：

```powershell
cmake -S libraries/NanoUI -B build/nanoui-tests -DNANOUI_BUILD_TESTS=ON
cmake --build build/nanoui-tests --config Release
ctest --test-dir build/nanoui-tests -C Release --output-on-failure
```

已有 Ninja/MinGW 环境可在首次配置时增加 `-G Ninja -DCMAKE_BUILD_TYPE=Release`。
更换生成器或编译器时使用新的构建目录，不复用旧 CMakeCache。
产物和运行方式见[快速开始](getting-started.md)。

## 3. STM32 固件

```powershell
cd firmware/stm32
cmake --preset MinSizeRel
cmake --build --preset MinSizeRel
```

默认 ELF 为 `firmware/stm32/build/MinSizeRel/crabit_st.elf`（相对仓库根目录）。
MinSizeRel 预设明确启用保留供电的 STOP2：屏幕单独休眠与调试常亮关闭，
旧 companion 自动启动跳过，运行态 QMI 探针关闭。默认运行页面仍是表盘。
QMI 运动唤醒使用 PB0、192 mg；USB 热初始化保留描述符计数复位修复。
这些是已验收板卡的配置，不表示可直接套用到不同接线的硬件。
预设用 Ninja 和 `cmake/gcc-arm-none-eabi.cmake`，编译器通过 PATH 查找。
也可在仓库根目录用工程入口逐项执行：

```powershell
python tools/engineering.py run stm32 configure
python tools/engineering.py run stm32 build
```

当前链接脚本有意保留换芯后的 256 KB Flash 配置。不要为“修复生成文件”改回历史 `.ioc` 的容量，
也不要未经板卡确认就把它用于更小的芯片。CubeMX 重新生成后，还要检查手工 USB Composite 注册、PMA、SOF 与 CMake 接入，不能覆盖掉这些代码。

## 4. BW16：Arduino IDE 入口

产品 sketch 是 **`firmware/rtl8720dn/NanoUI/NanoUI.ino`**，不是 STM32 目录，也不是独立 FirstApp 示例。

共享库先打包一次：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package-nanoui-arduino.ps1
```

产物是 `build/NanoUI-library.zip`。在 Arduino IDE 中选择“项目 → 加载库 → 添加 .ZIP 库”，安装这个 ZIP，
然后打开产品 sketch，选择 Realtek AmebaD 的 **Ai-Thinker BW16** 板配置。
共享库变动后重新打包安装，避免 IDE 仍在使用旧副本；留意重复安装库提示。

普通构建使用已生成的资源，不需要临时运行字体工具。不要通过修改 SDK include path、把整套 HAL 复制到 Arduino 库，或到处复制头文件来解决依赖。

**本节只说明 IDE 的编译入口，不构成已闭环的 IDE 上传教程。** 当前尚未为公开发布文档化并验收
“定位 IDE 实际上传镜像 → 检查 → 保证上传对象未替换”的流程，因此不建议据此直接点击上传。
CLI 的 BuildPath 与 IDE 上传目录不是同一个位置，检查 CLI 镜像不能替 IDE 上传背书。

IDE 的“验证成功”不能证明镜像符合本项目保护边界。准备烧录的实际打包镜像必须经过下述检查，
且必须确认它就是 IDE 最终要上传的那一份；不要把另一个输出目录里的 bin/通过记录套到本次 IDE 上传。

## 5. BW16：带检查的 CLI 构建

回到仓库根目录，使用现有包装入口：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-rtl8720dn.ps1 -ArduinoIdeDefaults -BuildPath build/bw16-ide-check
```

该入口会给 Arduino CLI 指定共享 NanoUI 库，并检查 `km0_km4_image2.bin`。
默认 FQBN 是 `realtek:AmebaD:Ai-Thinker_BW16`。命令完成后产物留在指定 BuildPath，不会自动导出到 sketch 上传目录，也不会烧录。

SDK 有可变的公共构建状态。**BW16 构建必须串行**：包装器之间有互斥，但外部 IDE 不一定遵守它。
不要同时运行 IDE 验证与 CLI，也不要多个工作目录并行编译 BW16。

若要检查自己明确选定的实际镜像，脚本参数为：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-bw16-image.ps1 -ImagePath build/bw16-ide-check/km0_km4_image2.bin -StrictTarget
```

它输出长度、物理结束地址、SHA256 和伴随 JSON。此检查会写检查报告，不会烧录。
起点 `0x6000`、第一保留边界 `0xFC000`、严格长度目标 `0xF4000`；真实 FTL 起点另见[硬件说明](hardware.md)。
检查失败必须停止，不能放宽边界来让结果“通过”。Arduino 报告的 sketch 大小、ELF 文件大小和打包镜像长度不是同一指标。

`-Profile` 的其他 lab 配置只用于编译/占用测量，带 `DO_NOT_FLASH.txt`，不作为使用教程。
它们的旧无线 App 没有获得本次共存设计的硬件验收。详见profile 记录。

## 6. Flutter 客户端

```powershell
cd apps/mobile
flutter pub get
flutter analyze
flutter test
flutter run -d windows
```

Windows runner 会接入已有的本地 NanoUI 模拟器，不需要手工找旧 DLL 复制。
这里只启动客户端；不会自动给 BW16 上电。真实 BLE 路径仍受当前候选限制。

## 7. 烧录前后保留什么

在操作者确认烧录前准备一份记录：

```text
源码完整 SHA：
STM32 ELF 路径 / SHA256 / 实际芯片容量：
BW16 sketch SHA / profile / SDK 版本：
BW16 packed bin 路径 / SHA256 / 边界检查结果：
已知可用的成对回退版本：
本次唯一想验证的变化：
```

烧录后分别检查 STM32 UI、BW16 UI、退出/再次进入、BLE 广播、配对、STM32 回显、设置和 USB。
只通过其中一项不能写成“整机通过”。不认识接口、恢复方式或镜像身份时，先停止操作并查[故障排查](troubleshooting.md)。
