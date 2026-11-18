# BW16 App 包格式

`app/apps/` 下每个 App 一个目录，便于单独复制、审查和分享。App 的菜单标签、
C++ 类和 factory 只在 `app/arduboy30_registry.h` 登记一次；包内不再使用
`app.json`（旧的 JSON manifest 已取消，避免同一 id/class/name 重复登记）。

```text
apps/<category>/<id>/
├── app.h          App 类入口（唯一注册点见下）
├── README.md      操作、限制、上游版本、验证范围
├── LICENSE        上游/作者许可（不可省略）
├── src/           可选；只放 .h / .inc.h
└── assets/        可选；只读资源
```

`category` 目前使用 `games`、`wireless`、`system`。`id` 使用小写字母、数字和
连字符/下划线，例如 `hollow`、`ble-spam`，不能包含路径分隔符。

一个包只需要 `app.h`、`README.md` 和 `LICENSE`；`src/`、`assets/` 可选。菜单
元数据写在包内 README 和单一 C++ 注册里，没有 JSON manifest，也不需要每个
App 单独的 Context。

## 单一 C++ 注册

菜单实际使用 `app/arduboy30_registry.h` 的 const table，而不是运行时的 JSON
或自动生成清单。该表目前只服务 21 个 catalogue 游戏；`wireless`/`system`
包会被安全检查，但不会被 `--arduboy30` 选中或列出，启用方式需要单独的
launcher/profile 决策。一个游戏 App 需要：

```cpp
#include "apps/games/pong/app.h"

entryFor<PongApp>("pong", "Pong"),
```

条目必须同时受对应的 `CRABIT_ARDUBOY30_ENABLE_<ID>` 宏控制（选择构建只打开
这些宏）。注册检查会拒绝重复的 id、类和菜单名，也会检查 include 与 enable
宏是否成对存在。

普通 App 使用可失败的 `new (std::nothrow)` 工厂；只有 Arduboy catalogue 端口
经 `CrabitArduboy::tryCreate<T>()` 先准备按需 State。两种工厂都只在返回非空
时启动，失败时保持当前 App。

包内 README 记录原先 `app.json` 中的有价值信息：

- `version`：上游固定版本/提交；
- `requires`：依赖的兼容层或服务，例如 `arduboy2-compat`；
- `license`：SPDX 标识或明确的 `unknown`，并与包内 `LICENSE` 一致；
- `permissions`：`wifi`、`ble`、`radio-owner` 等；未声明时默认只能使用普通 UI；
- `mode`：仅 Arduboy 端口使用，统一为 `frame`；其他 App 不声明 mode。

## 约束

- 固件是**静态链接**，不是运行时加载二进制插件。分享 App 等于分享源码包，
  合并后重新编译。
- Arduino 会递归编译 sketch 下的 `.cpp`/`.ino`；App 源码使用 `.inc.h` 包含体，
  避免被单独编译和重复定义。`.inc` 后缀不会被 sketch 暂存复制。
- App 不应有可变文件级 `static`/namespace 全局状态；使用类成员，或进入时
  分配、退出时释放。无线服务等确有常驻需求的状态必须在 README 中说明。
- 不复制真实 `ble_auth.local.h`、私钥、绝对机器路径或设备凭据。
- 涉及 Wi-Fi/BLE/射频的 App 必须声明权限，并遵守现有 radio owner 规则。
- 不恢复 `app.json`；需要新的包元数据时写进 README 或单一 C++ 注册。

## 工具

```powershell
python tools/bw16_apps.py list
python tools/bw16_apps.py validate
python tools/bw16_apps.py scaffold games my-game
```

`scaffold` 复制 `apps/_template/`，生成一个类，并打印要加入
`app/arduboy30_registry.h` 的唯一注册行（含 enable 宏 guard）；不会创建
`app.json`，也不会自动改 launcher。`validate` 检查代码注册、入口、
README/LICENSE、`.inc.h` 约定和明显的安全问题；`list` 只列 21 个 registry
游戏，其他分类的目录包仍会被安全检查但不会出现在该列表。它不能替代编译、
实机、无线共存或法律审查。

新 App 模板位于 `apps/_template/`。
