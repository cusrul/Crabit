#pragma once
#include <NanoUI.h>

// Offline typography candidates only. No callbacks invoke radio/device services.
namespace CrabitUiReview {
inline Widget* toolFolder(bool ble) {
    static const uint8_t folder[] = {0x1f, 0xf8, 0x10, 0x08, 0x17, 0xe8, 0x18, 0x18,
                                     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18,
                                     0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x17, 0xe8,
                                     0x10, 0x08, 0x1f, 0xf8, 0,    0,    0,    0};
    NanoUI::EmbeddedVector<Widget*> rows;
    const char* labels[] = {"抓包", "信标", "雷达", "频谱", "伪装", "同步"};
    const char* details[] = {"握手监听", "广播测试", "设备追踪",
                             "信道流量", "设备广播", "数据上传"};
    for (int i = 0; i < (ble ? 1 : 6); ++i) {
        Widget* row =
            ble ? static_cast<Widget*>(
                      new Row({new SizedBox(6, 0), new Text("蓝牙广播", 8), new Spacer(),
                               new Text(">", 8), new SizedBox(6, 0)}))
                : new Row({new SizedBox(4, 0), new Image(folder, 16, 16), new SizedBox(4, 0),
                           new Column({new Text(labels[i], 8), new Text(details[i], 8)})});
        rows.push_back(new Button(new Container(row, 76, ble ? 22 : 28, 0, true, 4), []() {}));
        rows.push_back(new SizedBox(0, 4));
    }
    rows.push_back(new Button(new Container(new Text("返回", 8), 0, 14, 0, true, 2), []() {}));
    // Keep the current split-panel/22px-row layout. Static icon substitutes menu art only.
    return new Container(
        new Row({new SizedBox(4, 0),
                 new Container(new Column({new Spacer(), new Image(folder, 16, 16), new Spacer()}),
                               38, 56, 1, true, 4),
                 new SizedBox(6, 0), new Container(new ScrollColumn(rows), 76, 64)}),
        128, 64);
}
inline Widget* toolLabelRow(const char* label, const char* value = ">") {
    return new Container(new Row({new SizedBox(4, 0), new Text(label, 8), new Spacer(),
                                  new Text(value, 8), new SizedBox(4, 0)}),
                         128, 12);
}
inline Widget* buildToolsPage(int variant) {
    if (variant == 0) {
        return toolFolder(true);
    }
    if (variant == 1) {
        return toolFolder(false);
    }
    if (variant == 2) {
        return new Container(
            new Column({new Container(new Text("选择扫描方式", 8), 128, 12, 0, true),
                        new SizedBox(0, 2),
                        new Container(new Text("被动监听", 8), 120, 17, 0, true, 2),
                        new Container(new Text("系统扫描", 8), 120, 17, 0, true, 2),
                        new Text("确认开始  返回退出", 8)}),
            128, 64);
    }
    return new Container(
        new Column({new Container(new Text("信道40  网络16", 8), 128, 12, 0, true),
                    toolLabelRow("[ ] Openwrt", "40"), toolLabelRow("[ ] Openwrt", "9"),
                    toolLabelRow("[ ] 测试网络", "6"), new Text("每信道2秒  持续监听", 8)}),
        128, 64);
}
}  // namespace CrabitUiReview
