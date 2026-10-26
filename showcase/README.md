# 产品介绍与新手教程

这些文件随产品源码维护，网站从固定提交的显式白名单导入，不从工作区自动执行代码。

## 按目标阅读

- 已拿到手表：[快速使用](docs/start.md) → [连接 App](docs/connect.md) → [App 操作](docs/app-guide.md)。
- 想开始写界面：[环境搭建](docs/setup.md) → [第一个 App](docs/first-app.md) → [界面开发](docs/ui-development.md) → [字体图片](docs/resources.md)。
- 想理解整条链路：[系统导读](docs/system-walkthrough.md) → [Flutter 第一页](docs/flutter-first-page.md) → [客户端开发](docs/flutter-development.md) → [联动练习](docs/integration-lab.md) → [温度报告卡](docs/sensor-project.md)。
- 从硬件复刻：[复刻路线](docs/replicate.md)；更新设备先读 [更新与恢复](docs/update.md)，缺少已确认的接线和版本时不要自行刷写。

## 文件职责

- `product.json`：产品事实，不在这里保存验证码或凭据。
- `docs`：网站可展示的正文。
- `examples`：与教程相配套的代码；示例是否通过主机检查不代表硬件已验收。
- `media`：筛选后的图与来源清单；原始照片保留在本地忽略目录，不随站点发布。
- 维护者需补充：需要实物确认的资料。
- 文档缺口：待完成项与已做工作的边界。

更改教程或素材后，先提交产品仓库，再更新网站 `source-registry.json` 中的固定 SHA 并重新构建。
模型/Wasm 另有构建来源校验；修改产品资源后必须重新构建演示，不能只改版本标签。
