# 通信协议导览

本页是源码阅读入口，不把所有已定义 topic 都声明为当前可用功能。权威定义在
[`firmware/shared`](../../firmware/shared/)；完整链路还要同时看双端处理器和 Flutter 请求管理。

## 三个不能混淆的层

1. **BLE GATT**：Flutter 与 BW16 之间的连接、写入和通知。
2. **应用消息**：topic、value、请求编号、配对和业务响应。
3. **CrabitWire v1**：BW16 与 STM32 内部 UART 的二进制封装。

收到 GATT 写入完成不代表消息已转发，UART 发送完成也不代表另一端解码或执行。
广播名 `Crabit-Watch` 不是设备唯一身份，更不是认证凭据。

## BLE 服务

| 用途 | UUID |
| --- | --- |
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX：客户端写入 BW16 | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX：BW16 通知客户端 | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

应用桥接消息使用紧凑 JSON 字段，例如以下是结构示意，而不是未经认证可执行的功能承诺：

```json
{"i":17,"t":"sys/settings","v":""}
```

`i` 是关联编号，`t` 是 topic，`v` 是值。BLE MTU、分包和接收累计需要由当前实现处理；
不能假设一个 JSON 或配对命令必然一次 GATT 写完。`TEST:BLE_PROBE` 是 BW16 本地测试路径，不能代替 STM32 往返测试。

## UART 帧：CrabitWire v1

定义见 [`crabit_wire.h`](../../firmware/shared/crabit_wire.h)。多字节整数用小端序：

```text
frame = payload_length:u16 + payload + crc16:u16
payload = version:u8 + topic_length:u8 + value_length:u8
          + request_id:u16 + topic_bytes + value_bytes
```

- version 为 1。
- topic 最长 47 字节，value 最长 191 字节；这里是字节数，不是 Unicode 字符数。
- payload 最长 243 字节，frame 最长 247 字节；短消息按实际长度编码。
- 线上字符串不带结尾 NUL。
- CRC 初值 `0xFFFF`、多项式 `0xA001`，只覆盖 payload。
- 解码器检查版本、长度与 CRC；CRC 是传输检错，不是加密、认证或防重放。

响应应保留可关联请求的 request_id。是否匹配成功、是否超时和是否执行失败需要请求管理层另行判断。

## 设置契约示例

[`settings_contract.h`](../../firmware/shared/settings_contract.h) 定义读取 `sys/settings` 和写入 `sys/settings/set`。
写入值使用 ASCII 分号分隔字段，例如：

```text
brightness=500;invert=0;screen_off_ms=30000;deep_sleep_ms=0
```

brightness 是 0–1000 的整数比例，invert 是 0/1；时间单位是毫秒，最大值 86400000。
`deep_sleep_ms=0` 表示禁用深睡。2026-10-22 的 SysPower 修正后，
`screen_off_ms=0` 禁用自动息屏；旧固件未正确实现这个零值语义，不能据此推断其行为。
设置页仍只提供已有的正数息屏时间选项；旧存储中的零值作为自定义值保留。
当前 `ScreenOnlyWake` 验证构建还会在编译期禁用 STOP2，见
息屏与供电验证。
未知字段、重复字段或非法值整批拒绝；不要一部分成功后再把其余忽略。
以上说明数据格式，不能越过当前后台链路故障直接认定远程设置已可用。

## 状态、ACK 与配对

应分别观察：发现广播 → GATT 连接 → 服务/通知可用 → 应用认证 → 请求入队 → 业务结果。
`QUEUED`/传输 ACK 不是 `READY` 或操作完成；超时也不能自动解释成设备主动拒绝。
旧后台路径还存在 `ATTACH TIMEOUT`；新候选取消自动 ATTACH，先做带编号的启动后 UART 检查，尚待实机验证，详见[状态](status.md)。

项目的应用层 PAIR/令牌确认与蓝牙 SDK 的 SMP/bond 不是同一机制。
`PAIR DIAG` 的 RAM 确认只改变 RAM，不写 Preferences，断电后不能靠这次确认恢复令牌。
未锁定 RAM 策略的旧/UI 路径仍会在 `configureBleOnce()` 读取 Preferences，实体确认后也保留持久写入分支。
新 MODE_BLE 候选在该函数之前锁定 RAM 策略，跳过旧令牌读取并阻止物理确认落入写入分支；
启动后的 PING/PONG 不授予认证，也不提供配对确认。两条策略分别描述，不能把整个固件说成全 RAM-only，
也不能据此宣称持久化分支或 SDK 内部存储已安全验收。
历史同次上电的再次连接免确认，也不能写成永久配对成功。
配对内容可能跨多个 GATT 包，屏幕显示收到首段不等于完整令牌收到。
更不能为了调通实验而跳过确认、默认信任首次出现的设备或暴露令牌到日志。

当前 BLE 验证码候选在普通 `sys/mode=BLE` 路径使用单次 `AUTH:<6 位数字>` GATT 写入，
BW16 在 MODE_BLE 中按本地编译配置检查；错误验证码立即断开，未提交验证码最长等待 15 秒。
该路径不发送 `PAIR`，也不写入配对令牌 Preferences；它**不是** BLE SMP/bond、加密链路或防重放认证。
验证码是固定编译值并经未加密的 GATT 写入传输，因此仅是当前本地验证候选的访问门槛，
不能作为对抗监听/重放的产品安全边界。Flutter 诊断会话在应用侧限制为只读 topic；
有效验证码本身不会让 BW16 获得区分诊断页与产品页的上下文，外部 BLE 客户端拿到验证码后
仍可尝试发送现有应用协议命令。准确范围、构建配置及未完成的实机验收见候选记录。

新增 topic 前，先定义请求/结果/失败、长度、编号、超时、断连行为和权限，再实现三端。
涉及电源、启动和 Flash 的命令不能仅因有统一协议就直接开放给客户端。
