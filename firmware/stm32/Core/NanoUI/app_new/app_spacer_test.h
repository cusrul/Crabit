#ifndef APP_SPACER_TEST_H
#define APP_SPACER_TEST_H

#include <NanoUI/NanoUI.h>

/**
 * SpacerTestApp: 布局压力测试页
 * 用于展示 嵌套布局、混合字库、弹性权重 以及 溢出保护。
 */
class SpacerTestApp : public Application {
public:
    const char* getAppName() override { return "STRESS TEST"; }

    Widget* buildUI() override {
        std::vector<Widget*> items;

        // --- 1. 复杂页眉 (TTF + BDF 混排) ---
        items.push_back(new Row({
            new Spacer(1, 4),
            new Text(STYLE_ART("LAYOUT"), 16),
            new Spacer(),
            new Text("V2.0", 12),
            new Spacer(1, 4)
        }));

        items.push_back(new SizedBox(0, 4));

        // --- 2. 嵌套 Column 压力测试 ---
        // 在一行里放两个纵向排列的卡片
        items.push_back(new Row({
            new Spacer(),
            // 左卡片
            new Container(
                new Column({
                    new Text("CPU", 12),
                    new Spacer(),
                    new Text("45%", 12),
                    new ProgressBar(0.45f, 30, 4)
                }),
                40, 36, 2, true, 2
            ),
            new Spacer(1, 8), // 核心：弹性间距且最少保留 8px
            // 右卡片
            new Container(
                new Column({
                    new Text("FPS", 12),
                    new Spacer(),
                    new Text("60", 16),
                    new Spacer()
                }),
                40, 36, 2, true, 2
            ),
            new Spacer()
        }));

        items.push_back(new SizedBox(0, 8));

        // --- 3. 权重分配展示 (1:2:3) ---
        items.push_back(new Row({ new Text(" FLEX RATIO (1:2:3)", 12) }));
        items.push_back(new Container(
            new Row({
                new Button(new Text("A", 12), [](){}),
                new Spacer(1),
                new Button(new Text("B", 12), [](){}),
                new Spacer(2),
                new Button(new Text("C", 12), [](){}),
                new Spacer(3),
                new Button(new Text("D", 12), [](){})
            }),
            128, 18, 0, true, 0
        ));

        items.push_back(new SizedBox(0, 8));

        // --- 4. 极端超长行 (验证溢出不崩塌) ---
        items.push_back(new Text(" OVERFLOW GUARD", 12));
        items.push_back(new Container(
            new Row({
                new Text("BEGIN", 12),
                new Spacer(1, 20), // 哪怕挤爆了也留 20px
                new Text("THIS_IS_A_VERY_LONG_STRING_THAT_BLEEDS_OUT", 12),
                new Spacer(),
                new Text("END", 12)
            }),
            128, 15, 0, true, 2
        ));

        items.push_back(new SizedBox(0, 10));

        // --- 5. 交互式滚动长列表 ---
        for (int i = 1; i <= 5; i++) {
            char buf[16]; sprintf(buf, "LIST ITEM %02d", i);
            items.push_back(new Button(
                new Container(
                    new Row({
                        new Text(buf, 12),
                        new Spacer(),
                        new Text(">", 12)
                    }),
                    110, 14, 2, false
                ),
                [](){}
            ));
            items.push_back(new SizedBox(0, 2));
        }

        items.push_back(new Spacer(1, 20)); // 底部撑开

        // --- 6. 底部固定返回区域 ---
        items.push_back(new Row({
            new Spacer(),
            new Button(
                new Container(new Text(" [ EXIT TEST ] ", 12), 0, 18, 0, true, 4),
                [](){ AppManager::exitCurrentApp(); }
            ),
            new Spacer()
        }));

        items.push_back(new SizedBox(0, 10));

        return new Container(
            new ScrollColumn(items),
            128, 64, 0, false, 0
        );
    }
};

#endif