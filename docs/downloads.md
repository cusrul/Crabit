# Crabit v0.2.0-preview.1 下载

本页对应 [GitHub v0.2.0-preview.1 预发布](https://github.com/cusrul/Crabit/releases/tag/v0.2.0-preview.1)。这是预览版，不是稳定版，也不代表下列组合已完成实机验收。请按设备型号和用途选择文件；不要把固件交叉烧入另一颗芯片。

## 发布包

Windows 安装器构建入口为 `tools/build-windows-installer.ps1`，可将完整运行目录打包成单个 `Crabit-Setup-…-x64.exe`。安装器采用当前用户安装，包含 DLL 与资源；WebView2 和 VC++ 运行库仍是系统前置条件，不会静默安装。安装包未签名，Windows 可能提示未知发布者。以 Release 实际附件为准，不能将尚未上传的本地构建当作已有下载。

| 文件 | 内容与适用范围 |
| --- | --- |
| `Crabit-STM32-v0.2.0-preview.1.zip` | STM32L432CBU6（256 KiB）的 ELF 与 BIN；BIN 烧录地址为 `0x08000000`。 |
| `Crabit-BW16-v0.2.0-preview.1.zip` | BW16 应用 packed image，镜像起点为 `0x6000`，不是 `0`。只用于已有兼容 bootloader 的设备；不含 bootloader，也不用于空白设备的完整量产烧录。 |
| `Crabit-Windows-x64-v0.2.0-preview.1.zip` | Windows x64 客户端完整目录；需要本机已安装 WebView2 Runtime，包内不附带 Runtime。解压后运行包内应用，不要只取出 EXE。 |
| `Crabit-Android-v0.2.0-preview.1-debug-signed.apk` | Release 构建的 Android APK，但由 Android Debug 证书签名；不是生产签名，也没有手机实机验收。 |

下载文件及 GitHub 提供的校验值见[预发布页面](https://github.com/cusrul/Crabit/releases/tag/v0.2.0-preview.1)。校验后再使用，保留原有可用固件与客户端以便回退。STM32/BW16 烧录、复位和上电均由设备操作者执行；本页不替代对应硬件版本的操作指导。

## BW16 验证码与烧录边界

此公开 BW16 包由源码提交 `06ab0b5` 构建，公开构建的初始 BLE 应用验证码为 `000000`。这是公开默认值，不是秘密，也不代表 BLE 链路加密或 SMP 加密。请只在可信的 BLE 环境使用；不适合暴露于不可信环境的设备。已有设备在实际烧入这个 BW16 包之前仍保留原有固件和验证码，不能据此推断所有设备的验证码都是 `000000`。

此包仅更新 BW16 应用镜像，要求设备已有兼容 bootloader。不要对空白设备使用，不要执行全片擦除，也不要把 bootloader 当作本包的一部分。更换私有验证码时，需自行编译并烧录对应应用镜像；不能通过 App 设置更改设备端验证码。此发布构建配方不包含空白设备的完整制造/首次烧录流程。

## Android 安装提示

APK 虽以 Release 模式构建，签名仍是 Android Debug 证书。未来若改用不同证书签名，Android 可能要求先卸载旧版才能安装；卸载可能删除 App 本地数据。升级前先导出或备份需要保留的数据。

## 许可说明

Flutter 客户端保留现有 FlutterBluePlus 插件；请先阅读仓库的[第三方许可与声明](../THIRD_PARTY_NOTICES.md)及其商业条款引用。本次提供下载不表示购买了商业许可、取得额外授权或完成了法律审查。各组成部分仍受其适用的许可证与声明约束。

关于如何连接 App，见[连接配套 App](../showcase/docs/connect.md)；固件更新说明见[固件更新与恢复](../showcase/docs/update.md)。
