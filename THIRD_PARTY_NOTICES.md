# 第三方来源与许可清点

这是为公开发布准备的**初步来源清单**，不是完成的依赖审计，也不对各许可的兼容性作结论。
Crabit 原创内容采用 GPL-3.0-only；第三方内容不因此被重新授权，范围及组合限制见[授权说明](LICENSING.md)。
不要删除或改写第三方文件中的版权/许可头，分发源代码或二进制前还需检查具体包和版本要求。

## 仓库内已定位的声明

| 内容 | 原有声明入口 | 当前记录 |
| --- | --- | --- |
| BW16_BLE_Spam 1.0.0 | [MIT](libraries/BW16_BLE_Spam/LICENSE)、[来源与使用](libraries/BW16_BLE_Spam/README.md) | 维护者提供并确认可分发；保留源码 MIT 声明与原作者元数据 |
| ARM CMSIS | [CMSIS LICENSE](firmware/stm32/Drivers/CMSIS/LICENSE.txt) | Apache-2.0 文本；仍需保留各文件声明 |
| STM32L4 HAL | [HAL LICENSE](firmware/stm32/Drivers/STM32L4xx_HAL_Driver/LICENSE.txt) | 指向包级条款；无适用包条款时给出 BSD-3-Clause 回退，不统一改写许可 |
| STM32 USB Device Library | [USB LICENSE](firmware/stm32/Middlewares/ST/STM32_USB_Device_Library/LICENSE.txt) | SLA0044 Rev6/October 2025 回退条款原样保留；Crabit 原创部分另有[限定链接许可](LICENSE-EXCEPTIONS.md)，ST 部分不改成 GPL |
| FreeRTOS | [FreeRTOS LICENSE](firmware/stm32/Middlewares/Third_Party/FreeRTOS/Source/LICENSE) | 保留原有 MIT 型授权文本与版权 |
| three.js 离线资源 | [vendor LICENSE](apps/mobile/assets/watch_sim/vendor/LICENSE) | MIT，保留作者版权与声明 |
| NanoUI 默认字体输入 | [字体 SOURCES](libraries/NanoUI/assets/fonts/SOURCES.md) | 各字体来源不同；WenQuanYi 头包含 GPL v2 与字体嵌入例外，需逐项检查 |
| Fusion Pixel Font zh-Hans 8px BDF | [字体来源与版本](libraries/NanoUI/assets/fonts/fusion-pixel-8px/README.md)、[OFL 1.1](libraries/NanoUI/assets/fonts/fusion-pixel-8px/OFL.txt)、上游 `LICENSES/` 目录 | TakWolf，release `2026.10.16`；原样保留字体许可与上游字形来源 notices |
| CastleBoy 移植 | [MIT](firmware/rtl8720dn/NanoUI/app/castle_boy/LICENSE) | 上游 `jlauener/CastleBoy`，固定提交 `1d758fdd862d6269dc459661779c52d75cc87f98`；copyright 2016 dir3kt，README credits ZappedCow / Increment；多份游戏实现与上游文件匹配 |
| Arduboy3D 移植 | [MIT](firmware/rtl8720dn/NanoUI/app/arduboy3d/LICENSE) | 上游 `jhhoward/Arduboy3D`，固定提交 `929db9f3429cc20a318934099d992f1219a081bd`；James Howard 2019；保留原有 a1k0n 技术来源注记 |
| Hopper 改编 | [MIT](firmware/rtl8720dn/NanoUI/app/LICENSE-Hopper.txt) | 上游 `obono/ArduboyWorks`，固定提交 `d4b1f041789dcd1d71907654e4025d613b4ab420`；OBONO 2016–2026；本项目为改编，不宣称逐文件原样复制 |

这张表不覆盖所有源文件。ST 包级许可、其他传感器驱动、旧 App、参考目录和复制进来的实现也需继续追溯原始来源。
`reference/` 里的许可证不能直接套用到名称类似、来源未确认的活动代码。

## 安装在仓库之外的依赖

- Realtek AmebaD SDK、Arduino core 与工具链：编译时使用，不因本仓库准备公开就默认可以重打包；应记录所用版本和原始授权。
- Flutter/Dart 与依赖包：清单见 [pubspec.yaml](apps/mobile/pubspec.yaml) 和 [pubspec.lock](apps/mobile/pubspec.lock)。每个依赖的源码/二进制和 notices 要按实际锁定版本清点。
- Python 资源工具依赖：见 [requirements.txt](libraries/NanoUI/tools/requirements.txt)，可选的视频处理依赖另外核对。
- 桌面开发环境、编译器和平台 SDK：安装许可与再分发条款另行遵守，不复制维护者机器环境作为发行包。

## 作者素材与生成文件

3D 手表 GLB、源 Blender/机械模型、截图、照片、图标、字体输入和生成的资源应分别记录作者、来源和分发范围。
“由工具生成”不会自动消除输入字体或图像的使用条件；源码扫描得到的字体子集也不是可随意重授权的新通用字库。

当前模型存在于 `apps/mobile/assets/crabit_collection.glb`，但这不替代源模型、装配文件及第三方零件授权确认。
发布前建议为实际包含的每组素材补齐可核对的来源记录，而不是只写“均来自网络”。

## 维护方式

新增或升级依赖时，同时更新版本、原始来源、许可文件位置与分发注意项。
未确认来源的内容不应带着“稍后再说”的假设进入公开发行包。
最终许可与公开范围由项目所有者确认，参见[候选与发行边界](docs/release-candidate.md)。

## 2026-10-27 核对结果

- `flutter_blue_plus 2.2.1` 及其受同一条款约束的 federated 包不是普通 BSD
  依赖。已安装的锁定版本含 FlutterBluePlus License 1.3，商业用途（包括个人
  商业用途）要求取得商业许可，且禁止重新授权。当前上游主分支条款可能与
  锁定版本不同；不能据此覆盖旧版本文本。尚无商业授权凭据或与本项目 GPL
  组合分发的许可确认，不将客户端标记为“可无条件商用分发”。
  来源：[锁定版本](https://pub.dev/packages/flutter_blue_plus/versions/2.2.1)、
  [上游许可](https://github.com/chipweinberger/flutter_blue_plus/blob/master/packages/flutter_blue_plus/LICENSE.md)。
- 独立 Windows 实现 `flutter_blue_plus_winrt 0.0.18` 为 Himchan Park 的 MIT
  代码；这不会把它依赖的主包和 platform interface 一并变成 MIT。
- `flutter_scene 0.9.2-0`、`flutter_scene_importer 0.9.0-0`、
  `flutter_3d_controller 2.3.0` 保留各自 MIT 声明。
  `flutter_inappwebview 6.1.5` / Windows `0.6.0` 保留 Apache-2.0；
  Windows 原生插件使用 WebView2 SDK `1.0.2792.45`、WIL `1.0.231216.1`
  和 nlohmann.json `3.11.2`，其原生 notices 也需要随二进制保留。
  WebView2Loader.dll 不等于整个 WebView2 Runtime，不打包用户浏览器缓存。
- 默认 `5x8.bdf`、`6x10.bdf` 文件头声明 public domain；`gb24st.bdf`
  带有 Institute of Software, Academia Sinica 1988 的允许使用、修改及分发
  条款，必须保留声明，不能笼统写成 WenQuanYi GPL 字体。
  `wqy12.bdf` / `wqy16.bdf` 文件头写 GPL v2 with font embedding exception，
  不能未经证据把它们写成 OFL。生成字体仍保留输入字体的条件。
- 手表模型、照片和完整 AD 通用库集合没有完成逐项权利证明。文件留存及
  维护者同意保留，不代替第三方授权；未把它们统一重新授权成 GPL，也未
  删除维护者的本地源文件。

本次没有为解决许可证问题更换已实测的 BLE 插件、升级 SDK、重生成字体或
删减产品功能。候选包与本地恢复包不等于授权全部清理完成的公开发行包。

游戏源码 MIT 不代表取得第三方游戏系列的商标、角色或美术授权。WenQuanYi
上游嵌入例外的措辞针对 document，不能未经确认就推广到所有固件。上述
来源补齐解决的是缺失 notices，不把这些边界删除或伪装成完整法律审查。

开发机安装好锁定依赖后，可收集实际 Flutter 包的声明：

```text
python tools/collect_flutter_notices.py apps/mobile/.dart_tool/package_config.json apps/mobile/pubspec.lock --output build/dependency-notices
```

输出仅有包名、锁定版本、原样许可文件及其 SHA256，不包含本机缓存绝对路径。
它单列缺失声明及商业条款复核项，不将“已收集文件”等同于“允许商用”。
Flutter SDK 中由仓库根许可覆盖而包内没有独立许可的项目，以及原生 NuGet
依赖，仍需结合对应 SDK/包的声明处理。
