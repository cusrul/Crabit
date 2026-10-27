# 架构与源码入口

Crabit 不是“一个工程编译出所有平台”。它由两份嵌入式固件、一个共享 UI 库和一个客户端组成；
共享的是框架和协议契约，不是芯片 SDK、板级状态或同一个构建输出目录。

## 各层负责什么

| 层 | 维护位置 | 负责 | 不负责 |
| --- | --- | --- | --- |
| NanoUI 核心 | `libraries/NanoUI/src/NanoUI/product/` | Widget、RenderObject、布局、导航、控件与运行时 | HAL、Arduino、电源、BLE、具体传感器 |
| STM32 产品 | `firmware/stm32/` | 主控 UI、App、传感器、USB、BW16 供电和启动协调 | 替 BW16 执行无线 SDK |
| BW16 产品 | `firmware/rtl8720dn/NanoUI/` | 启动模式、无线服务、本机 App 与 UI 接管 | 绕过 STM32 随意重配板级电源/显示 |
| 共享契约 | `firmware/shared/` | 帧格式、设置与消息数据定义 | 自行启动任务或持有设备 |
| Flutter | `apps/mobile/` | 用户操作、连接状态、本机存储、诊断、3D 与桌面预览 | 云端后台、远程唤醒已断电 BW16、自动证明硬件操作成功 |

## 一次真实通信经过哪里

```text
Flutter 页面 → 客户端连接/请求管理 → BLE GATT
                                        ↓
                              BW16 协议与无线 owner
                                        ↓
                              UART / CrabitWire v1
                                        ↓
                             STM32 消息分发与设备服务
                                        ↓
                               原路返回业务结果
```

BW16 自己回复的 BLE probe 不经过 STM32。因此，probe 成功只证明到 BW16 的局部链路，不能证明设置写入、传感器读取或完整回显通畅。
CRC、请求编号、配对认证、SDK 无线状态和页面提示也各自解决不同的问题，详见[协议](protocol.md)。

## NanoUI、App 与资源

新 App 从 `<NanoUI.h>` 进入，使用 `Application::buildUI()` 组合 Widget。
平台建立 Canvas、InputDriver、时钟和字体 provider，并负责更新/绘制。
App 状态、控件树和 RenderObject 的寿命需要协调，但不应由应用层重新实现一套布局引擎。

当前两端产品的兼容头会转发到同一份维护中的原框架。Arduino 安装的 ZIP 是该库的分发副本，不是第二份权威源码。
`experimental/` 下保留的其他实现不属于默认产品核心；嵌套旧框架和备份树已从当前源码树移除，历史内容可从 Git 恢复。
资源按 App 扫描和生成，不把两个相似字体子集合成公共的万能字库。见[NanoUI](nanoui.md)与[资源工具](assets.md)。

### 名字相近的 App 目录

| 目录 | 实际角色 | 整理规则 |
| --- | --- | --- |
| STM32 `Core/NanoUI/app_real/` | 默认嵌入式产品 App；由当前 CMake 接入 | 产品维护入口 |
| STM32 `Core/NanoUI/app_desktop/` | 原桌面模拟器及布局观察工具 | 不是无用备份，保留独立资源配置 |
| BW16 `NanoUI/app/` | 当前 Arduino 产品 App | 产品维护入口 |
| BW16 `NanoUI/app_example/` | 示例资源配置和工具可选扫描范围 | 不混入默认产品字库；保留示例用途 |
| `libraries/NanoUI/examples/` | 独立框架接入示例 | 不依赖 Crabit 的硬件 App |

未参与当前构建的 STM32 `usb_hid/`、`usb_hid_new/`，以及旧 `CMakeLists_STM32.txt`、`Core/NanoUI/app_new/` 和同层旧 `examples/` 已退役，历史内容可从 Git 恢复。当前 USB 实现位于 `USB_DEVICE/App`、`Target` 和 `Composite`；不要删除 ST 中间件的 CustomHID 类。

字体输入、生成资源与共享 UI 核心是三件事。已有生成器仍按各产品配置扫描 App，
不能因为目录或字库看起来相似就合并生成输出；当前整理也不迁移模型或输入字体。

## 任务和所有权

STM32 的 `NanoUI_MainLoop` 持有导航、Widget/RenderObject 和显示；Modem、传感器任务通过消息或快照交接，不直接触碰 UI。
共享 I2C 需要现有互斥边界。USB 异步发送也有自己的缓冲与忙/失败结果，不能把“已调用发送”理解为“主机已接收”。

BW16 保留 BLE owner 和无线协调代码，不能因此推断所有旧 App 都已经安全迁移。
旧独占无线 App 与实验 payload profile 不等于经验证的共存设计。IDE 默认 profile 是 Wi-Fi 工具 + Hopper 试验组合，独立网络同步默认关闭，天气由手机提供。
正常 BLE 路径不再依赖历史 `ATTACH` 握手，验证码链路已有用户烧录后的基本使用反馈。六位验证码仅是应用层门槛，不建立 BLE SMP/bond，也不加密链路；新改动仍需独立回归。

## 不能顺手改变的边界

- STM32 控制的 BW16 电源、启动 GPIO、UART 切换和时序。
- AXP2101 供电路径、OLED 接管与归还、Flash 烧录桥。
- BW16 镜像与保留区/FTL 边界。
- 实际芯片容量与现有 STM32 链接器配置。

这些不是普通代码清理项。修改需要专门设计、可恢复固件和设备操作者验证，不能由桌面测试代替。

## 进一步看代码

- [STM32 工程说明](../../firmware/stm32/README.md)
- [BW16 工程说明](../../firmware/rtl8720dn/README.md)
- [NanoUI 独立库](../../libraries/NanoUI/README.md)
- [Flutter 工程](../../apps/mobile/README.md)
- [当前状态和历史证据](status.md)
