# App 资源：字体、图片与生成文件

NanoUI 把字体和图片数据编进应用，但资源生成不是 C++ 编译的前置步骤：
普通构建使用已生成并纳入源码管理的 `.h/.cpp`；只有修改 App 文本、字体或
图片时，才需要运行资源工具。生成物属于消费它的 App，不属于共享框架，也
不应由一个 App 覆盖另一个 App 的资源目录。

本指南按仓库快照 `549f3a3` 的工具与 FirstApp 配置编写。它描述生成器行为，
不宣称各产品当前资源完全相同，也不承诺字体的通用使用或再分发权利。

## 工具实际做什么

工具位于 [`libraries/NanoUI/tools`](../../libraries/NanoUI/tools/README.md)：

- `font_builder.py` 读取项目 JSON，扫描 App 源码和维护中的 NanoUI
  `src/NanoUI/product/`，提取配置范围内的字符串和字号，检查输入字体覆盖，
  并生成 `nanoui_assets.h/.cpp` 及默认字体 provider。
- `resource_gui.py` 帮助创建/选择配置，并打开字体生成界面。
- `asset_builder.py` 打开原有图片/动画编辑工具。图片资源输出复用生成资源
  文件，字体与图片数据块协同写入；未知格式会被拒绝，而不是静默覆盖。

扫描器是轻量文本扫描，不是 C++ 编译器：默认 `scan_mode` 为 `all`，扫描源码
字符串字面量；`visible` 模式只匹配部分常见绘制/文本形式。运行时拼出的文本、
翻译字符串或自定义文本函数可能不会被发现，可用 `extra_text`、`sizes` 和
`text_helpers` 显式补充。它不会扫描 `experimental`、生成目录、build 目录和
备份目录。换句话说，扫描通过不等价于运行时每条路径都覆盖完整。

## 先用应用自己的配置

在 App 项目中放置 `nanoui-assets.json`。相对路径以 JSON 文件所在目录为基准，
不依赖当前 shell 目录：

```json
{
  "app_dirs": ["src/apps"],
  "output_dir": "src/generated"
}
```

这是 FirstApp 自带配置的简化形态。`app_dirs` 可以是目录或目录数组，必须存在；
输出目录由应用管理。用库工具执行检查或生成：

```powershell
python -m pip install -r ../../tools/requirements.txt
python ../../tools/font_builder.py --config nanoui-assets.json --check
python ../../tools/font_builder.py --config nanoui-assets.json
```

以上相对工具路径对应仓库的 `libraries/NanoUI/examples/FirstApp/` 工作目录；
在其他 App 中使用时，应将 `../../tools/` 换成你所使用的 NanoUI 库工具路径。
配置中的相对路径仍以 JSON 为基准。生成图片可运行
`python ../../tools/asset_builder.py --config nanoui-assets.json`，随后在图形工具
中编辑/导出。Python 3.10+ 与 Pillow 用于资源生成；Tkinter 仅供 GUI 使用，
导入视频另外需要 `opencv-python`。这些依赖不进入普通 C++ 构建。

工具默认将 BDF 字体输入定位到库的 `assets/fonts/bdf`。可用的默认字号为
8、10、12、16、24；项目文本和框架文本所需字符必须在所选输入中可找到。
尤其要注意：当前随库的 `5x8.bdf` 不含中文；扫描到中文并不意味着它能生成
中文字形。若 BDF 缺少字符或 `STYLE_ART` 需要 TTF 路径，显式提供你选定的字体：

```json
{
  "app_dirs": ["src/apps"],
  "output_dir": "src/generated",
  "ttf_font": "fonts/your-font.ttf"
}
```

这只是配置示例，字体文件需要由应用维护者自行取得并确认有权使用。工具不会
自动选择 Windows 系统字体或悄悄用别的字体替代。实际可用字符取决于字体文件，
不是 BDF 文件名或字号标签。

其他可选项包括 `fonts_dir`、`extra_text`、`sizes`、`scan_mode`（`all`/`visible`）、
`bdf_alignment`（`baseline`/`center`）、`font_provider` 和 `text_helpers`；遗留
布局可另设输出路径。具体字段、错误条件和生成机制以
[`tools/README.md`](../../libraries/NanoUI/tools/README.md) 与
[`nanoui_tools/config.py`](../../libraries/NanoUI/tools/nanoui_tools/config.py) 为准。

## 检查后再替换

1. 先确认 JSON 指向的是当前 App 的源码和生成目录；不要把产品/私有目录写进
   可公开发布的配置或文档。
2. 运行 `--check`。它会扫描并在内存中打包，不写输出文件。对缺失字形、未知
   数据或配置错误先修配置/输入，不要手工拼接生成字体表。
3. 生成后检查变更范围，只保留预期 App 的资源文件，并在该 App 的正常构建中
   编译它们。若生成结果异常，停止，不把半成品复制到其他平台目录。
4. 工具在替换前暂存输出并保留 `.bak`，Python 可捕获异常时会回滚已替换文件；
   多文件写入不是断电级原子事务。生成期间不要并发运行两个写入器，发生中断
   时先比较当前文件和备份，再决定恢复。不要把手工合并字体子集当作修复。

默认 provider 开启时，应用应编译生成的资源 `.cpp`；自带字体 provider 的应用
设置 `"font_provider": false`，避免链接两个 provider。资源目录要进入该应用的
include path。检查构建输入比只看生成器退出码更重要。

## 仓库已有资源不是一份通用合并字库

`libraries/NanoUI/assets/fonts/bdf/` 是库内工具的默认输入集合；FirstApp 的
生成文件位于 `libraries/NanoUI/examples/FirstApp/src/generated/`，由该示例自己
拥有。固件树里还保留各自的 App 配置和生成资源。平台之间存在相似字体输入，
并不意味着应该把平台生成字典合并或互相覆盖。此处只说明路径和所有权，不对
任何第三方字体、图片或未来打包发布作统一授权承诺；具体来源限制见
[`assets/fonts/SOURCES.md`](../../libraries/NanoUI/assets/fonts/SOURCES.md)。
