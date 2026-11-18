# BW16 App 目录

`firmware/rtl8720dn/NanoUI/app/` 是当前 Arduino 产品 App 入口。目录按职责分层：

```text
app/
├── apps/            可分享 App
│   ├── games/       游戏与 Arduboy 移植
│   ├── wireless/    Wi-Fi / BLE App
│   ├── system/      系统类 App（预留）
│   ├── _template/   新 App 模板
│   └── README.md    App 包格式和注册规则
├── compat/          可复用兼容层
│   └── arduboy/
├── core/            启动器、共享无线/协议辅助代码
│   ├── launcher.h
│   ├── wifi_hack_core.h
│   └── ...
└── README.md
```

- 新 App 必须使用 `apps/<category>/<id>/` 包结构（`app.h` + `README.md` +
  `LICENSE`），并在 `app/arduboy30_registry.h` 的单一 C++ 表中登记一次；不再
  创建 `app.json`。校验命令为 `python tools/bw16_apps.py validate`。
- 既有无线 App 暂时以单文件形式放在 `apps/wireless/`；后续迁移到独立目录时
  使用 `README.md` + `LICENSE` 和单一 C++ 注册，不恢复 `app.json`。
- `core/launcher.h` 只负责菜单登记和构建开关；App 实现不要放回 `app/` 根目录。
- `compat/arduboy/` 是复用层，不是 App；不要把游戏源码放进兼容层目录。

游戏与 Arduboy 的详细规则见
[apps/games/README.md](apps/games/README.md)。App 包格式见
[apps/README.md](apps/README.md)。
