# 构建、产物与烧录

从GitHub全新克隆的前置配置、已测试工具版本和实际结果见[复现构建](../reproduction.md)。

本文命令默认在仓库根目录执行，除非代码块先切换目录。这里只给出构建与检查流程；
烧录、复位、进入下载模式和供电由设备操作者执行。下面的双区维护工具只适用于操作者已确认的镜像组合与恢复路径，不是通用稳定版刷机入口。

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

### 已验证板卡的双区维护入口

维护者已确认双区方案可行；本地旧原型曾在 COM6/COM14 完成主区和扩展区写入与 checksum 校验。
这些操作记录不等于所有游戏、无线共存或所有板卡已通过验收。
可维护入口是 `tools/flash_bw16.py`，旧 `build/bw16-two-range-flash/` 原型保留作恢复参考。
新入口增加了配对检查、有限超时与失败诊断；仅通过主机测试，尚未重新实机烧录。

先进行离线检查（不开串口）：

```powershell
python tools/flash_bw16.py --main build/<本次构建>/stock-pack/km0_km4_image2.bin --extension build/<本次构建>/games-extension.bin --report build/<本次构建>/report.json
```

必须使用同一次 split 构建的主镜像、扩展镜像和报告；工具在打开串口前检查长度、地址和两个 SHA256，
并持有已验证的字节快照，不会在擦除后重新读取可能被替换的文件。
报告的 `status: PASS` 只表示构建证据，不会把 `flashable: false` 自动升级为硬件批准。
上述 `build/` 文件不随 Git 克隆分发；可用 `tools/build_bw16_split_games.py` 在新的输出目录构建另一对。
默认 flashloader 来自当前用户 `LOCALAPPDATA` 下的既有 AmebaD tools 1.1.3，可用 `--flashloader <path>` 指定已有文件；不会安装或改写 SDK。

操作者进入烧录桥并按「开始烧录」后，可只读监听当前端口：

```powershell
python tools/flash_bw16.py --probe --port COMx --seconds 6
```

仅在明确授权本次设备操作后，使用同一对文件并追加：

```text
--execute --port COMx --retire-ota2
```

工具保留旧原型的协议字节与操作顺序：擦 OTA2 签名扇区 `[0x106000, 0x107000)`，
擦写主区（从 `0x6000`、长度不超过 `0xF4000`），擦写扩展区 `[0x107000, 0x1F8000)`，
最后分别校验主区与扩展区。中间 `[0xFA000, 0x106000)` 不写，boot 镜像不写；
**OTA2 退役后没有 A/B 回退**。不得把两个 BIN 拼接成跨越保留区的连续镜像。
工具固定使用主机端 115200，不协商或修改 STM32 的实际 UART 速率；适用性以已确认的桥和板卡为准。

同步/普通 ACK/校验响应等待为 10 秒，擦除 ACK 为 120 秒，串口写超时为 10 秒；
`--wait N` 在打开端口后、首次同步前等待 N 秒。超时或掉线不会自动重试、复位或切换供电。
若已发出擦除命令，工具会提示 Flash 可能不完整，不能假定 BW16 可启动；
由操作者重新进入 STM32 烧录桥，再用同一对镜像完整重写。写入校验通过后仍需手动退出桥接并检查实际运行。

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
