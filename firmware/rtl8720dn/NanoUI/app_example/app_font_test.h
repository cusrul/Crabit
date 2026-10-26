#ifndef APP_FONT_TEST_H
#define APP_FONT_TEST_H

#include "../src/NanoUI.h"

/**
 * FontTestApp: 专门用于测试混合字库兼容性和布局精度的页面
 */
class FontTestApp : public Application {
public:
    const char* getAppName() override { return "FONT TEST"; }

    Widget* buildUI() override {
        return new PageTransition(
            new Container(
                new Column({
                    // 1. 标题展示
                    new Row(std::vector<Widget*>{
                        new Spacer(),
                        new Text(STYLE_ART("混合字库测试"), 16), // 矢量标题
                        new Spacer()
                    }),
                    
                    new Spacer(), // 纵向间距自动分配

                    // 2. BDF 原生点阵展示 (12px)
                    new Container(
                        new Row(std::vector<Widget*>{
                            new Text(" [BDF 12px] ", 12),
                            new Spacer(),
                            new Text("原生点阵：清晰锐利", 12),
                        }),
                        128, 13, 0, true, 2
                    ),

                    new Spacer(),

                    // 3. BDF 原生点阵展示 (16px Unifont)
                    new Container(
                        new Row(std::vector<Widget*>{
                            new Text(" [BDF 16px] ", 12),
                            new Spacer(),
                            new Text("大点阵：标准易读", 16),
                        }),
                        128, 17, 0, true, 2
                    ),

                    new Spacer(),

                    // 4. TTF 矢量字体展示 (16px)
                    new Container(
                        new Row(std::vector<Widget*>{
                            new Text(" [TTF 16px] ", 12),
                            new Spacer(),
                            new Text(STYLE_ART("矢量渲染：圆润平滑"), 16),
                        }),
                        128, 17, 0, true, 2
                    ),

                    new Spacer(),

                    // 5. 混合布局压力测试
                    new Row({
                        new Text("A", 12),
                        new Text(STYLE_ART("B"), 16),
                        new Text("混合", 12),
                        new Text(STYLE_ART("排版"), 16),
                        new Spacer(),
                        new Button(new Text(" [BACK] ", 12), [](){ AppManager::exitCurrentApp(); })
                    }),
                }),
                128, 64, 0, false, 0
            ),
            TransitionType::SlideFromRight
        );
    }
};

#endif