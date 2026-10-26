#ifndef APP_COMPLEX_TEST_H
#define APP_COMPLEX_TEST_H

#include "NanoUI/NanoUI.h"
#include <vector>

class ComplexTestApp : public Application {
public:
    const char* getAppName() override { return "NEST_TEST"; }
    
    Widget* buildUI() override {
        std::vector<Widget*> list_items;

        // 1. 混合层级测试：Column 嵌套 Row
        list_items.push_back(new Row({
            new Container(new Text("左", 12), 20, 20, 0, true, 2),
            new Spacer(),
            new Column({
                new Text("顶部", 10),
                new Text("底部", 10)
            }),
            new Spacer(),
            new Container(new Text("右", 12), 20, 20, 0, true, 2)
        }));

        list_items.push_back(new SizedBox(0, 4));

        // 2. 深度嵌套测试：Row -> Column -> Row
        list_items.push_back(new Container(
            new Row({
                new Text("深层", 12),
                new SizedBox(4, 0),
                new Column({
                    new Row({ new Text("甲1", 10), new SizedBox(2,0), new Text("甲2", 10) }),
                    new SizedBox(0, 2),
                    new Row({ new Text("乙1", 10), new SizedBox(2,0), new Text("乙2", 10) })
                })
            }),
            120, 32, 2, true, 4
        ));

        list_items.push_back(new SizedBox(0, 4));

        // 3. 极端对齐测试
        list_items.push_back(new Row({
            new Button(new Container(new Text("按钮1"), 40, 16, 0, true, 2), [](){}),
            new Spacer(),
            new Button(new Container(new Text("按钮2"), 40, 16, 0, true, 2), [](){}),
        }));

        list_items.push_back(new SizedBox(0, 8));
        list_items.push_back(new Button(new Container(new Text(" 返回 "), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));

        return new Container(new ScrollColumn(list_items), 128, 64, 0, false, 0);
    }
};

#endif
