# 从 GitHub 复现构建

先安装目标所需的工具链，再克隆源码。仓库不附带编译器、Flutter SDK、Realtek SDK或已安装的Windows开发组件。

```powershell
git clone https://github.com/cusrul/Crabit.git
cd Crabit
git rev-parse HEAD
```

建议使用较短、自己有写入权限的目录。下面未注明工作目录的命令都从仓库根目录执行。
普通构建使用已生成字体和图片，不需要系统中文字体或重新生成资源。

## 已实际验证的环境

| 工具 | 本次版本 |
| --- | --- |
| Windows / Visual Studio | Windows，VS2022 Community，MSVC 14.43 |
| CMake / Ninja | 3.30.2 / 1.12.1 |
| Windows GNU C++ | GCC 14.2.0 |
| STM32交叉编译器 | GNU Arm Embedded 10.3-2021.10 |
| Flutter / Dart | 3.41.6 / 3.11.4 |
| Arduino CLI / AmebaD core | 1.4.0 / realtek:AmebaD 3.1.9 |
| Python | 3.13.5；工程工具要求3.11或更新 |

这是一台已有工具链的Windows电脑上的全新源码克隆验证，不是全新Windows系统的自动环境安装验收。
没有升级SDK或依赖，也没有使用其他工作目录的旧构建产物。

## 1. 没有手表：先跑 NanoUI

已安装CMake、Ninja和PATH中可用的g++时：

```powershell
cmake -S libraries/NanoUI/examples/FirstApp -B build/first-app -G Ninja -DCMAKE_CXX_COMPILER=g++ -DBUILD_TESTING=ON
cmake --build build/first-app --parallel 2
ctest --test-dir build/first-app --output-on-failure
.\build\first-app\NanoUIFirstApp.exe
```

没有g++但有VS2022桌面C++组件时，可在一个新的输出目录使用VS生成器，不要把两种生成器混在同一目录。
FirstApp资源测试应通过，运行后会出现NanoUI示例窗口。

框架测试：

```powershell
cmake -S libraries/NanoUI -B build/nanoui-tests -G Ninja -DCMAKE_CXX_COMPILER=g++ -DNANOUI_BUILD_TESTS=ON
cmake --build build/nanoui-tests --parallel 2
ctest --test-dir build/nanoui-tests --output-on-failure
```

## 2. Flutter Windows客户端

安装Flutter和VS2022的“使用C++的桌面开发”组件；VS Code编辑器不能代替这套编译组件。

```powershell
cd apps/mobile
flutter pub get --enforce-lockfile
flutter analyze
flutter test
flutter build windows --release
.\build\windows\x64\runner\Release\crabit_watch_app.exe
```

分发程序时需要整个Release目录中的DLL和data资源，不能只复制exe。
Flutter会重生成本机平台插件注册文件；本次没有升级pubspec.lock中的依赖。

## 3. STM32

将arm-none-eabi-gcc/g++、CMake和Ninja放进当前终端PATH：

```powershell
cd firmware/stm32
cmake --preset MinSizeRel
cmake --build --preset MinSizeRel --parallel 2
```

ELF在 `firmware/stm32/build/MinSizeRel/crabit_st.elf`。
实际板卡是256KiB Flash器件，文件名保留历史命名，不能据此改小链接区。
本节只构建，不烧录，不改变电源、USB、STOP2或OLED流程。

## 4. BW16：必须先配置自己的验证码

安装Arduino CLI及Realtek AmebaD 3.1.9，板型是 `realtek:AmebaD:Ai-Thinker_BW16`。

```powershell
Copy-Item firmware/rtl8720dn/NanoUI/ble_auth.local.example.h firmware/rtl8720dn/NanoUI/ble_auth.local.h
```

用编辑器打开新副本，将 `CRABIT_BLE_AUTH_CODE` 的 `REPLACE` 改成自己选择的六位数字字符串。
不要覆盖自己已有的配置，也不要提交这个本地头文件。未创建时构建脚本会提前提示缺失。
原样保留REPLACE可能完成编译，但不能通过设备验证码认证，不能直接作为可用固件烧录。

回到根目录构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-rtl8720dn.ps1 -ArduinoIdeDefaults -BuildPath build/bw16
```

脚本显式使用仓库内NanoUI和BW16_BLE_Spam库，不依赖维护者电脑的Arduino库目录。
仍可用BleSpamLibrary参数显式指定另一份库；一般不需要。
SDK内部状态可变，BW16构建必须串行，不要同时让IDE编译。

脚本会检查实际打包的 `km0_km4_image2.bin`，超出保护边界就失败。
不能用Arduino显示的2MiB容量替代本项目镜像边界，也不能放宽检查来“通过”。

Arduino IDE用户另运行 `tools/package-nanoui-arduino.ps1` 生成NanoUI ZIP并安装，
同时安装仓库的 `libraries/BW16_BLE_Spam`。打包后的NanoUI内包含GPL许可文本。
完整烧录注意事项见[构建与烧录](guide/build-and-flash.md)。

## 本次检查结果（2026-10-27）

| 目标 | 实际源码提交 | 结果 |
| --- | --- | --- |
| NanoUI框架 | 6b3a0b2298ef0baf8d3ee4ddca29d7ed9dce513e | 29/29测试通过 |
| FirstApp | 同上 | 编译、1项资源测试、窗口启动通过 |
| native NanoUI模拟器 | 同上 | VS2022 Debug构建、1项测试通过 |
| STM32 MinSizeRel | 同上 | 全新构建通过；Flash 200752B，主RAM 56608B |
| Flutter | 同上 | analyze通过；202项测试通过、1项条件跳过、0失败；Windows Release构建通过，首屏与3D手表实际显示 |
| BW16仓库内依赖 | f29bfe246f13cbf5dbb93250f5705265736f077d | 再次全新克隆后构建和镜像检查通过 |
| NanoUI Arduino ZIP | 同上 | 打包通过，确认包含LICENSE |

Flutter跳过项位于 `test/device_center_render_test.dart`，条件为 `output == null`；没有将它计作通过。
Windows构建有MSVC转换/WebView2警告，BW16有第三方库架构元数据大小写及SDK比较警告；本次不是零警告构建。

BW16本次使用未填入真实设备码的模板，只作编译验证，产物已标记不要烧录。
打包镜像长度 `0xF2000`，物理结束 `0xF8000`；
SHA256：`59a157a1a2f3a07a82878143c15d5f8ea1eb208642dac2c68e1cea6a3833c6db`。
使用仓库内库与原外部库时的打包镜像哈希一致。

未做：新机器完整安装、Linux/macOS/Android/iOS构建、真实BLE连接、USB实机、烧录及整机回归。
编译通过也不代表第三方许可兼容性已经解决；特别是ST USB库的组合分发限制，见[授权说明](../LICENSING.md)。

## 遇到失败时

- 缺编译器：先确认目标工具已安装且在当前终端PATH，不改SDK源码来绕过。
- 缺ble_auth.local.h：按上面的步骤创建私有配置；不要从别人那里复制真实设备码。
- 找不到BW16BLESpam.h：确认使用完整的新仓库和当前构建脚本，不使用仅含ino的孤立副本。
- 生成器冲突：换一个新的build目录，不覆盖已有工作成果。
- Flutter依赖下载失败：检查pub.dev/NuGet/GitHub网络访问；不要直接升级锁定依赖或关闭证书校验。
