# Crabit 编码与可读性规范

适用范围是本项目维护的代码，而不是替第三方、SDK 和生成器重写输出。
统一的是团队规则和维护习惯；不同语言使用各自习惯，不强求相同缩进和命名形式。

## 规范来源与本地取舍

| 语言 | 参考 | 本项目采用 |
| --- | --- | --- |
| C/C++ | [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) | clang-format 19.1.1，Google 基础、4 空格、100 列、同行左括号；保留 include 顺序 |
| Dart | [Effective Dart](https://dart.dev/effective-dart/style) | Dart 3.11.4 的 dart format、2 空格；已有 flutter_lints 继续生效 |
| Python | [PEP 8](https://peps.python.org/pep-0008/) | Black 24.4.2、4 空格、100 列；项目最低语法目标 Python 3.11 |

这是有明确例外的项目规范，不宣称完整照搬 Google。Google 的命名与库使用规则
不能覆盖 NanoUI 已有的 Flutter 风格 API，也不要求更换容器、异常、RTTI 或分配器。
不为风格改变协议、公开 API、SDK 名称、板级符号或二进制兼容性。

## 命名、布局与注释

- C++ 类型用 `UpperCamelCase`，新增 NanoUI 方法沿用 `lowerCamelCase`；命名空间和文件
  使用有意义的名称，新增文件用 `lower_snake_case`。现有公开名字不做全仓机械重命名。
- 新增私有成员用尾下划线，模块私有可变全局沿用 `g_`；常量用 `kName`，宏用
  `UPPER_SNAKE_CASE`。C/SDK 适配接口保持接口所要求的命名，不混改生成符号。
- Dart 遵循官方类型、成员、文件命名；Python 函数/变量用 `snake_case`，类型用
  `UpperCamelCase`，常量用 `UPPER_SNAKE_CASE`。
- 一条语句表达一个步骤，展开有副作用的条件、循环和错误处理。不要把多个状态修改
  挤在一行。位操作、标准循环和清晰的简单访问器不必为了行数再包装。
- `buildUI()` 保持声明式 Widget 组合。重复的产品外观抽为小型 App 组件，不引入
  布局代理层或强制所有控件改成 builder。头文件实现不一概迁到 cpp。
- 注释解释原因、执行上下文、单位、边界和所有权；中文和英文都可使用。删除失效的
  “新增/终极/完美修复”叙述，用当前约束替代；不为去掉 emoji 而清空有用说明。
- 时间、容量和协议字段标明单位；业务分支使用有语义的枚举/常量，不依赖菜单位置。
- 不把每个坐标都抽成全局常量；只为重复、相关或有业务意义的尺寸命名。

## 职责和接口

- 框架负责布局、渲染、控件和导航；App 负责状态、业务与页面；平台负责设备能力。
  核心不能依赖 Arduino、HAL 或某个 Crabit 服务。
- 将输入处理、状态推进、数据消费、绘制表达为可辨认的步骤。只在形成清楚职责时提取
  函数/模块，不按文件行数机械拆分，也不增加空壳 service/controller 转发链。
- 公共 API 说明：调用方上下文、参数借用/接管、返回值表示入队还是完成、失败时谁清理。
  裸指针不一律改成拥有对象；区分 `borrowed` 和 `owned`，生命周期必须能从接口看懂。
- 同一份业务状态只有一个权威所有者，UI 状态是它的投影。`volatile` 不是跨任务同步。
- 行为修复、结构调整、格式化分开提交；不借整理触碰电源/启动/UART/OLED/Flash/FTL。

## 自动检查与格式化

从仓库根目录运行：

```powershell
python tools/style.py list
python tools/style.py check
python tools/style.py fix --language python
python tools/style.py check --language dart
```

配置在根 `.editorconfig`、`.clang-format`、`pyproject.toml`；精确工具版本和已迁移范围在
`tools/style_scope.json`。`check` 不改源文件；`fix` 只修改登记目标。
缺工具或版本不匹配会失败，不记作检查通过。可用进程级 `CRABIT_CLANG_FORMAT`、
`CRABIT_BLACK`、`CRABIT_DART` 指向已有工具，不修改全局 PATH 或共享 SDK。

Python 格式工具可装入自己的项目虚拟环境，依赖见 `tools/style-requirements.txt`。
CI 首先运行已迁移 Python 格式检查；C++/Dart 先在本地使用已有工具验证，不能把尚未
接入 CI 的语言写成 CI 已覆盖。命名、职责、注释和所有权仍需代码审阅，formatter 不会检查它们。

## 全仓迁移清单与例外

| 范围 | 当前阶段 | 验证要求 |
| --- | --- | --- |
| Flutter `lib/`、`test/` 自维护 Dart | 第一批统一 dart format | formatter、analyze、全部 Flutter 测试 |
| 新的风格工具、菜单回归与恢复守卫 Python | 第一批 Black | formatter、单测、实际主机探针 |
| 三个自维护 Windows/C++ 测试工具 | 第一批 clang-format | formatter、相关编译与测试 |
| 共享 NanoUI product 核心、框架测试、主要两端 UI 页 | 已登记 clang-format；逐文件范围见清单 | 框架测试、页面绘制回归、双端编译；不做 API 重命名 |
| tools 与 NanoUI 资源生成工具 Python | 已登记 Black | AST 等价检查、工具与资源测试；不改生成模板和资产 |
| 未登记的无线服务、旧 App、实验入口 | 保留现有格式 | 不为风格扩大本轮改动；不是本轮发布阻断项 |
| STM32 生成入口、AXP2101、电源/启动/UART/OLED/Flash、链接配置 | 本轮保持字节不变 | 有专门设计与用户硬件验收后才可变更 |
| Drivers、Middlewares、外部游戏/驱动、reference、vendor | 保留上游风格 | 不混入自有代码格式提交，保留来源与许可证 |
| 字体 BDF、图像/模型、生成资产、构建输出 | 不手工格式化 | 修改配置/生成器，通过可复现生成更新 |

登记范围是分批实施机制，不是宣称其余代码已经统一。后续每批需同步迁移清单和检查范围。
不使用“全仓已规范化”描述仅通过部分目标的结果。

## 首批执行记录 — 2026-10-21

基线为 `ef20e1b`；回退引用为 `baseline/pre-code-style-2026-10-21`。
首批规范覆盖已登记的 78 个 Dart、5 个 Python、3 个主机 C++ 文件。只有需要调整的
文件发生格式变更，未改固件、共享框架源码、SDK、生成资源或历史证据。

Luna medium 实施限定范围的工具和测试；Sol high 独立检查路径/工具边界，Lead 审查并
实际运行工具。已修正初稿中的无效 Dart 参数、Windows 长命令、路径边界、配置缺失、
预发布版本匹配和资源目录判断问题。子 agent 自测不作为最终集成验证。

- 真实 clang-format/Black/dart format 检查通过；检查前后 86 个源文件 SHA256 一致。
- 风格工具 15 项测试通过；其他相关主机 13 项检查通过，含配置树 1000 次复用和回收。
- 首次相关测试曾因 Windows 临时 EXE 删除被占用而报错；未跳过，单项和整组重跑通过。
- Windows 预览 C++ 语法检查通过，未启动应用或操作设备。
- Flutter analyze 无问题；flutter test 为 140 项通过、1 项可选截图导出跳过。
- 公共文档检查 29 份、文档单测 13 项、工程流程单测 25 项通过。
- CI 只新增 Python 格式任务和工具单测，尚未执行远端流水线；不宣称 C++/Dart 已接入 CI。

该记录不意味着前述并发、Popup 所有权、UTF-8 或容量失败问题已经修复。

## 第二批执行记录 — UTF-8 与字体 provider

基线 `49aa68e`，行为修复 `06a788d`，随后单独格式化已触及的字体适配代码。
范围现为 78 个 Dart、6 个 Python、9 个 C++ 文件，共 93 个；不是全仓已统一。
共享 UTF-8 头与测试、两端字体 provider、主机 stub、字体接口纳入检查；生成的
provider 仍由模板产生，不直接格式化。Lead 实现，Sol high 独立审查，无 Luna 实施。

两端编译、27 项 NanoUI CTest 和真实 provider 边界测试通过。历史源码快照守卫
仍失败，完整 FirstApp 字库重新生成仍受现有缺字输入阻碍，未绕过或冒充通过。
详见本批证据与限制。

## 第三批执行记录 — BW16 回调所有权

基线 `25507d3`，行为修复 `2f39cff`。后续独立整理收件/解析生产头和测试，
不批量格式化产品 App。当前登记 14 个 C++、9 个 Python、78 个 Dart，共 101 个。
两处旧 UI-only 冻结测试随合法行为变更迁移为明确的 owner 边界测试，保留画面
回归；并新增完整生产 inbox 的常规 C++ 测试，而非继续堆叠源码哈希断言。

Lead 实施，Sol high 独立复核。范围、失败修复、SDK 证据及未解决问题见
回调收敛记录。这不是全仓规范化完成。

## 第四批执行记录 — Popup 与 Toast

基线 `b8174b6`，行为修复 `abbf9b8`；格式整理另交。登记范围扩至 17 个 C++、
9 个 Python、78 个 Dart，共 104 个。Popup 及新增常规 C++ 测试纳入检查，
没有借此整体重排 WindowManager 或重写 App。

Lead 实施，Sol high 独立复核；借用契约纠正、失败测试重跑、29 项 CTest、
双端构建及内存统计限制见本批记录。

## 最终限定整理 — 2026-10-21

行为修复 `a089c7a` 之后单独整理格式。登记 113 个 C/C++、85 个 Python、
78 个 Dart，共 276 个文件。共享 product 核心和常规 C++ 测试全量纳入；两端
主要 UI 页面按清单登记，不递归扫入外部游戏、平台硬件或历史实验。

87 个有差异的 C/C++ 文件核对非注释 token 等价（归一化宏续行与相邻字符串）；
74 个仅格式变更 Python 文件 AST 等价。两个页面源码守卫单独适配 formatter
产生的空格/换行，不降低原有顺序、错误分支或绘制断言；系统信息页两处乱码
注释改为当前含义，未改界面字符串。Flutter 仅重新检查，没有新源码变动。

这不是全仓重写或零告警承诺。受保护硬件、SDK、第三方和生成物保留既有格式。
本轮收尾后不再主动扩大审计；编译、产物与剩余发布边界统一写入最终验收卡。
