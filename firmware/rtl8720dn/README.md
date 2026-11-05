# RTL8720DN/BW16 固件

目标开发板：Ai-Thinker BW16，芯片为 Realtek RTL8720DN。

Arduino 工程位于 `NanoUI/`，入口是 `NanoUI/NanoUI.ino`，使用 Realtek AmebaD core。
新用户先读[当前状态](../../docs/guide/status.md)和[完整构建指南](../../docs/guide/build-and-flash.md)。
Arduino IDE 默认构建选择 Wi-Fi 工具 + Hopper 试验 profile；独立网络同步默认关闭，天气由手机中继。其他显式 profile 不代表可直接作为产品固件。
已有公开预览版下载；维护者于 2026-11-05 确认近期修改实机通过，下一套本地候选正在固定，尚未更新公开附件。六位验证码不是 BLE SMP 配对或加密机制。详见[当前状态](../../docs/guide/status.md)和[下载说明](../../docs/downloads.md)。

从仓库根目录执行带共享库与镜像检查的构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-rtl8720dn.ps1 -ArduinoIdeDefaults -BuildPath build/bw16-ide-check
```

Arduino IDE 需要安装仓库生成的共享 NanoUI ZIP，步骤见构建指南；不要只复制 `.ino`。
真正的 UI 核心在 `libraries/NanoUI`，STM32 和 BW16 使用同一份维护中的实现；产品 App、资源和平台适配各自独立。

BW16 构建必须串行，不能与另一个 CLI/IDE 构建同时占用 SDK。lab profile 仅供编译/空间测量，不作烧录建议。
打包镜像保护检查通过也不等于硬件验收，烧录与设备操作由用户完成。
