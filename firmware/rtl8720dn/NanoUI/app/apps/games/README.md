# BW16 游戏目录与 Arduboy 兼容层

## 目录

```text
app/apps/games/
├── README.md
├── hopper/
│   ├── app.h              Native NanoUI remake
│   └── LICENSE
├── arduboy3d/
│   ├── app.h              Wrapper
│   └── ...                Upstream sources
├── castle_boy/
│   ├── app.h
│   └── ...                Upstream sources
├── hollow/
│   ├── app.h
│   ├── LICENSE
│   ├── README.md
│   └── ...                Imported sources (.inc.h)
├── samegame/
└── lasers/

app/compat/arduboy/
├── Arduboy2Compat.h       New compatibility core
├── ArduboyAudioCompat.h   Shared mono game-tone adapter
├── ArduboyCompatApp.h     Frame-driven App base
└── legacy/                Old Arduboy2/App used by accepted games
```

`app/apps/` is the shareable app area. `app/core/` contains shared launcher and
wireless infrastructure; `app/compat/` contains reusable compatibility layers.

此外，21 个 catalogue 端口也位于 `app/apps/games/<id>/`；它们只由
`tools/build_bw16_split_games.py --arduboy30` 选择编译，默认六游戏菜单不包含。
每个端口包是 `app.h` + `README.md` + `LICENSE`，没有 `app.json`。

## 兼容层

| 层 | 使用者 | 状态 |
| --- | --- | --- |
| `Arduboy2Compat` + `ArduboyCompatApp` | Hollow Seeker、SameGame、Lasers、Castle Boy、Arduboy 3D 以及 21 个 catalogue 端口 | 唯一入口；Hopper 为独立实现 |
| `legacy/ArduboyCore.h` + `legacy/ArduboyApp.h` | 无 | 保留作参考，不再编译 |

`legacy/` 下的文件只为对照旧实现保留；新游戏不要引用。所有移植统一使用
倾斜方向、PAT 单按 A、双按 B、长按退出。

## 已实现与未实现

已实现：

- 128×64 单色 framebuffer、裁切、像素/线/矩形/圆/圆角矩形/三角形。
- 5×7 ASCII 文本、`setCursor`/`setTextSize`/`setTextWrap`/`setTextColor`。
- `pollButtons`、`pressed`、`justPressed`、`justReleased` 和 60 Hz 帧率控制。
- `SpritesCompat` 的 overwrite/self-mask/erase/plus-mask/external-mask。
- `ArduboyTonesCompat`、`ArduboyPlaytuneCompat` 到 STM32 PWM 的单声道音频映射，
  见下节。
- 每个端口逻辑上独立的 1024 字节 RAM EEPROM；不写真实 Flash/FTL。13 个存档
  端口按需分配会话槽，8 个零存档端口带访问 guard；槽在挂起时保留、析构时
  提交，复位/断电仍会丢失。
- 可选的倾斜输入；默认关闭，避免未经校准的轴向改变游戏方向。
- Arduboy 状态按需分配：同一 port id 只允许一个活实例，挂起时保留，析构时
  释放；普通 App 不需要每 App Context。

### 单声道音频

游戏里的 `tone()`/`tones()`/`playScore2()` 只登记播放意图；真正的串口发送由
`ArduboyCompatApp::onUpdate` 里的一次有界 `CrabitArduboyAudio::tick()` 完成，
复用现有 App 帧循环和 `Protocol` 出口，没有新线程、RTOS 队列或第二套调度器。

- 线上契约：`Topics::AppTone`（`app/tone`），ASCII `"频率,时长"`，频率
  100..5000 Hz，时长 10..250 ms，停止为 `"0,0"`，request_id 0，无 ACK。
  每秒最多 20 次起始；停止不受此预算限制。
- 支持 ArduboyTones 的 uint16 频率/时长对（0x8000 高音量位会被剥离），以及
  Playtune 的 `0x90`/`0x80`/等待/`0xD0` 标记变调/`0xE0` 重复/`0xF0` 停止，
  解析有固定步数和字节上限。
- 单声道压缩：和弦只保留最后一个音，重叠音互相替换，无音量控制；超出范围
  的频率会夹取，超过 250 ms 的音符每 200 ms 刷新一次以免被接收端截断。
- 挂起、卸载、析构、切换 App 和新游戏接管都会立即停止并丢弃旧指针；静音时
  不再登记新音。挂起时各 App 保存自己的声音开关，恢复时取回，不会被中间
  启动的其他 App 覆盖。
- 全新进入（重新启动 App）默认“声音开”，与上游默认一致。该默认值不写真实
  EEPROM，也不占用统一的音频标志槽，所以没有跨重启的音频开关持久化；断电
  或重新进入后不会记住上次的开关。
- 例外：Chiemagari 在自己的端口 RAM EEPROM 内保留 Arduboy 地址 2 的声音标志，
  因此挂起/重新进入后能恢复上次选择；该 RAM 记录随断电或复位丢失。其他端口
  不使用这个字节，也没有新增通用 EEPROM 槽或存档格式变更。
- 未做持久化声音设置、PCM/波形播放（Bananonsense 的 8 kHz 波形仍是空操作）、
  多声道混音和 RGB LED。

各端口 README 里早于本节的 “silent audio / 无声 stub” 描述是移植当时的历史记录，
以本节和 `ArduboyAudioCompat.h` 为准。

未实现：

- 多声道音频、音量控制、PCM、RGB LED、电池读取、压缩位图 `drawCompressed`。
- 持久化 EEPROM；RAM 记录只在运行期间保留。
- 所有 Arduboy 库 API；仅覆盖当前移植和常见小游戏需要的子集。
- 任何 BW16 后半段 Flash 实机执行结论；host/链接通过不等于实机验收。

## 新增一个移植

1. 按 `app/apps/_template/` 的包结构建立 `app/apps/games/<id>/`，填写
   `README.md`、`LICENSE` 和 `app.h`；不创建 `app.json`，也不需要每 App
   Context。
2. 放上游源码到该目录的 `src/` 或目录根部，记录固定提交、原项目地址、修改点。
3. 把 `.cpp`/`.ino` 文件改名为 `.inc.h`，在包装头文件里放进独立 namespace
   后只 include 一次。Arduino 会递归编译 sketch 下的 `.cpp`，不会自动编译
   `.h`；`.inc` 后缀不会被 sketch 暂存复制。
4. `MyArduboy` 或等价引擎继承 `Arduboy2Compat`；包装 App 继承
   `ArduboyCompatApp`，声明 `kPortId`、`kSaveBytes` 和 `PortState`，并在
   `prepareAdmission()` 里调用 `createPortState`、`attachSaveSlot`（零存档端口
   改用 `guardZeroSave`）、安装 sandbox 句柄和 `adoptPortState`。不要把引擎
   或游戏对象留作静态全局。
5. `setup()` 必须重置所有可变状态；不要留下跨 App 访问的静态初始化状态。
6. 不要使用 AVR 的 16 位函数指针、`pgm_read_ptr` 截断、真实 `EEPROM` 地址或
   阻塞式 `delay`。
7. 在 `app/arduboy30_registry.h` 登记一次（include、enable 宏和
   `entryFor<Class>("id", "menu label")`）。菜单直接取得函数指针；managed
   端口经 `CrabitArduboy::tryCreate<Class>()` 准备资源，普通 App 用可失败
   构造，成功后才启动；准入拒绝或分配器返回空指针时保持当前菜单。
   **实机 OOM 不保证可恢复**：当前 BW16 SDK 在堆耗尽时会进入停机 hook，
   host 的空指针回滚测试不代表实机耗尽能返回。再在
   `tools/build_bw16_split_games.py` 添加 namespace/类选择器与符号断言。
8. 加原生测试：标题/开始、基本输入、退出重进、边界和重绘；电脑端通过不等于
   实机通过。

## 验证

```powershell
python -m unittest tools.tests.test_arduboy_port_runtime `
  tools.tests.test_arduboy_ports_smoke `
  tools.tests.test_arduboy_ports_menu `
  tools.tests.test_build_bw16_split_games
python tools/bw16_apps.py validate
```

BW16 串行双区域构建：

```powershell
python tools/build_bw16_split_games.py --output build/bw16-split-games-local
```

实验产物仍不可直接烧录。主镜像与扩展镜像必须成对使用；中间配置、FTL、
OTA2 有效标记和直接 XIP 权限尚未完成实机验证。
