# Crabit

一块可以自己修改软硬件的 STM32L432 + BW16 双芯片手表，配有 Flutter 客户端。
NanoUI 使用 C++ Widget 组合界面，STM32、BW16 和 Windows 共用框架核心。

## 从哪里开始

- 从GitHub取得源码并验证构建：[复现说明](docs/reproduction.md)

- 已拿到手表：[快速使用](showcase/docs/start.md) · [连接客户端](showcase/docs/connect.md) · [App 操作](showcase/docs/app-guide.md)
- 开始开发：[环境搭建](showcase/docs/setup.md) · [第一个 App](showcase/docs/first-app.md) · [系统导读](showcase/docs/system-walkthrough.md)
- 编译设备：[构建与烧录](docs/guide/build-and-flash.md) · [硬件工程](hardware/altium/crabit-v2/README.md)
- 更多说明：[文档目录](docs/README.md) · [贡献指南](CONTRIBUTING.md)

## 源码目录

| 目录 | 内容 |
| --- | --- |
| firmware/stm32 | 设备控制、传感器、USB、供电与本机 UI |
| firmware/rtl8720dn | BW16 Arduino 固件、无线与独立 UI 模式 |
| firmware/shared | 双端通信与设备设置契约 |
| libraries/NanoUI | UI 核心、平台适配、示例、字体与图片工具 |
| apps/mobile | Flutter 客户端及本地模型预览 |
| hardware/altium | 可编辑 Altium 工程及完整引用库 |
| showcase | 图文使用和开发教程 |
| tools | 构建、资源与测试辅助工具 |

当前为开发源码快照，不是成对固件的稳定发行包。桌面预览不能替代实机验收；
烧录前确认实际板卡、镜像边界和所用固件组合，不直接更改供电、启动与 Flash 布局。

## 授权

本项目有权授权的原创内容采用 **GPL-3.0-only**，允许遵守条款的商业使用、修改和分发。
适用范围及尚未解决的第三方组合限制见[授权说明](LICENSING.md)。
第三方代码、字体、模型和硬件库保留原授权，不统一改标，见[第三方来源](THIRD_PARTY_NOTICES.md)。
