#ifndef APP_FONT_GALLERY_H
#define APP_FONT_GALLERY_H

#include "NanoUI/NanoUI.h"
#include <vector>

class FontGalleryApp : public Application {
public:
    const char* getAppName() override { return "FONT_GALLERY"; }
    
    Widget* buildUI() override {
        std::vector<Widget*> list_items;

        // 1. 12px BDF (标准点阵)
        list_items.push_back(new Column({
            new Text("12px BDF (标准点阵)", 12),
            new Text("像素对齐：你好世界", 12)
        }));

        list_items.push_back(new SizedBox(0, 6));

        // 2. 16px BDF (大点阵)
        list_items.push_back(new Column({
            new Text("16px BDF (清晰大字)", 12),
            new Text("高清像素：中文演示", 16)
        }));

        list_items.push_back(new SizedBox(0, 6));

        // 3. STYLE_ART (矢量转换)
        // 注意：STYLE_ART 会被 Python 标记为 ID:1，强制使用 TTF 生成
        list_items.push_back(new Column({
            new Text("STYLE_ART (矢量平滑)", 12),
            new Text(STYLE_ART("平滑渲染：艺术字体"), 16)
        }));

        list_items.push_back(new SizedBox(0, 8));
        list_items.push_back(new Button(new Container(new Text(" 返回 "), 0, 14, 0, true, 2), [](){ AppManager::exitCurrentApp(); }));

        return new Container(new ScrollColumn(list_items), 128, 64, 4, false, 0);
    }
};

#endif
