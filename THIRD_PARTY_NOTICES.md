# 第三方来源与许可清点

这是为公开发布准备的**初步来源清单**，不是完成的依赖审计，也不对各许可的兼容性作结论。
Crabit 原创内容采用 GPL-3.0-only；第三方内容不因此被重新授权，范围及组合限制见[授权说明](LICENSING.md)。
不要删除或改写第三方文件中的版权/许可头，分发源代码或二进制前还需检查具体包和版本要求。

## 仓库内已定位的声明

| 内容 | 原有声明入口 | 当前记录 |
| --- | --- | --- |
| ARM CMSIS | [CMSIS LICENSE](firmware/stm32/Drivers/CMSIS/LICENSE.txt) | Apache-2.0 文本；仍需保留各文件声明 |
| STM32L4 HAL | [HAL LICENSE](firmware/stm32/Drivers/STM32L4xx_HAL_Driver/LICENSE.txt) | 指向包级条款；无适用包条款时给出 BSD-3-Clause 回退，不统一改写许可 |
| STM32 USB Device Library | [USB LICENSE](firmware/stm32/Middlewares/ST/STM32_USB_Device_Library/LICENSE.txt) | 指向包级条款，回退文本为 SLA0044 Rev6/October 2025；**不是统一 BSD/MIT** |
| FreeRTOS | [FreeRTOS LICENSE](firmware/stm32/Middlewares/Third_Party/FreeRTOS/Source/LICENSE) | 保留原有 MIT 型授权文本与版权 |
| three.js 离线资源 | [vendor LICENSE](apps/mobile/assets/watch_sim/vendor/LICENSE) | MIT，保留作者版权与声明 |
| NanoUI 默认字体输入 | [字体 SOURCES](libraries/NanoUI/assets/fonts/SOURCES.md) | 各字体来源不同；WenQuanYi 头包含 GPL v2 与字体嵌入例外，需逐项检查 |
| Fusion Pixel Font zh-Hans 8px BDF | [字体来源与版本](libraries/NanoUI/assets/fonts/fusion-pixel-8px/README.md)、[OFL 1.1](libraries/NanoUI/assets/fonts/fusion-pixel-8px/OFL.txt)、上游 `LICENSES/` 目录 | TakWolf，release `2026.10.16`；原样保留字体许可与上游字形来源 notices |

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
最终许可与公开范围由项目所有者确认，参见公开发布清单。
