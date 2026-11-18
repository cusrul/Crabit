# STM32 固件

新用户入口：[硬件说明](../../docs/guide/hardware.md)、[构建与烧录](../../docs/guide/build-and-flash.md)、
[当前候选限制](../../docs/guide/status.md)。以下运行边界包含开发候选，不是稳定发行声明。
正常 BLE 已沿用取消 ATTACH 的启动路径；当前 `d0a949d` 的
审计记录中转已有用户真实数据闭环反馈。
旧 RAM/UART 实验保留为历史证据，不是最新操作入口。

CubeMX 历史器件名：`STM32L432KBU6`。用户已更换为 256 KB Flash 器件，
当前链接器的 256 KB 配置是有意保留，不在 USB 验证中重新生成工程。

这是独立的 CMake 工程，包含 CubeMX 配置、HAL、FreeRTOS、USB 和 STM32 侧
NanoUI 代码。构建入口是本目录的 `CMakePresets.json`。

```powershell
cmake --preset MinSizeRel
cmake --build --preset MinSizeRel
```

## 产品目录迁移

`Core` 保留 CubeMX/HAL/启动与未迁文件。D0 已完成 `product/board/`、
`product/features/companion/`；D1 的 `link_queue`、`settings_service`、
`diagnostic_service` 文件对已进入 `product/services/{link,settings,diagnostics}/`，
`audit_service` 进入 `product/features/audit/`；D2 将 `network_weather` 移入
`product/features/network/`，`companion_trace` 移入 `product/services/diagnostics/`。
D1 迁移与主 AI 集成验证通过，独立审查无阻塞项，本地提交 `3374fe6`：84 项针对性测试及 STM32 MinSizeRel 构建通过，ELF/BIN 与 D0 字节相同。
D2 已验收：102 项针对性测试、STM32 构建通过，ELF/BIN 与 D1 字节相同，独立审查无阻塞项。旧 weather/trace 夹具仍缺依赖或测试桩，不在通过集合内；未重跑预览、未操作硬件。
LinkQueue 是通信队列；Audit 是 BW16 无线审计 App 的记录中转 feature，
不是通用系统审计框架。设置、诊断和 Audit 共用既有链路，不另起 UART；
诊断中的 USB script 混杂留待后续拆分，本轮不统一 topic 注册机制。
批次与验证边界见架构地图。

## Runtime boundaries

- Standalone `BLE LINK` is owned by `product/features/companion/companion_ble.cpp` on the UI task, not by
  the page. STM32 keeps OLED; clock UP+DOWN for 1 s toggles the session. This
  hardware candidate holds STM32 awake and suppresses sensor bus recovery while
  BW16 is owned. Existing legacy autostart is explicitly tracked, never silently
  rebooted by START. See rollback and acceptance.

- `NanoUI_MainLoop` owns `WindowManager`, `AppManager`, the widget/render tree, and the display canvas.
- Modem and sensor tasks must not call UI or navigation APIs directly.
- Incoming coprocessor messages are validated in the Modem task and queued to `EventBus`; the UI loop consumes them with `EventBus::processPending()`.
- `AMEBA_LINK` uses existing FRAME callbacks so RUN returns to the UI dispatcher and releases I2C. STM32-only `ameba_session.h` gates boot/restore versus RUN; no platform state is added to shared NanoUI. RUN dispatch is limited to three messages per iteration. Low-power transitions and automatic PMU/bus recovery remain deferred throughout this session; legacy boot/off helpers still contain bounded delays. See candidate validation for unverified hardware gates.
- Hardware reset, low-power transitions, and bus reinitialization must be coordinated with the owning task before changing HAL state.
- Queue, semaphore, task, and UI-pool allocation failures must be reported and must not be followed by use of a null handle.

This is the current migration boundary. New cross-task features should use a fixed-size message or snapshot instead of directly touching UI globals.

`STM32_I2C_Lock` and `STM32_I2C_Unlock` define the ownership boundary for `hi2c1`; sensor work and UI/low-power work must hold this lock around I2C access. USB CDC transmit copies data into its owned buffer before starting the asynchronous transfer; callers must still handle `USBD_BUSY`/`USBD_FAIL`.

## Minimal USB composite candidate

`USB_DEVICE/Composite` adapts the existing classic USB Core to simultaneous CDC
and keyboard HID; CubeMX remains the HAL/project generator. Do not regenerate
away the manual class registration, CMake additions, class-ID-aware CDC adapter,
PMA map and SOF enable. No global SDK upgrade or USBX migration is required.
See design and hardware gates.
This is a fixed-text test, not the script/Flash-storage implementation. The old
BadUsb Flash executor remains disabled; do not test its store/erase commands.
The new RAM keyboard script candidate
uses the same SOF report owner and preserves the CDC/flash bridge path.
