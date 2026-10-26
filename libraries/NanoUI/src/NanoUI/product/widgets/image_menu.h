#ifndef WIDGETS_IMAGE_MENU_H
#define WIDGETS_IMAGE_MENU_H
#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/window_manager.h"
#include <cmath>

class RenderImageMenu : public RenderObject {
   public:
    const uint8_t** icons;  // 图标数组 (PROGMEM)
    int iconCount;
    int selectedIndex = 0;

    float currentScrollX = 0;  // 当前滚动位置
    int iconSize = 24;         // 图标尺寸 (假设 24x24)
    int spacing = 16;          // 图标间距

    RenderImageMenu(const uint8_t** i, int count) : icons(i), iconCount(count) {}

    bool canFocus() override { return true; }

    // 处理左右按键
    bool handleEvent(Event e) override {
        if (iconCount <= 0) {
            selectedIndex = 0;
            return false;
        }
        selectedIndex = NanoMax(0, NanoMin(selectedIndex, iconCount - 1));
        if (e.type == EVT_DOWN) {  // 我们把 DOWN 映射为“下一个”
            selectedIndex = selectedIndex + 1 == iconCount ? 0 : selectedIndex + 1;
            return true;
        }
        if (e.type == EVT_UP) {  // 把 UP 映射为“上一个”
            selectedIndex = selectedIndex == 0 ? iconCount - 1 : selectedIndex - 1;
            return true;
        }
        return false;
    }

    void performLayout() override {
        // 占据父容器分配的宽度，高度固定为图标高度 + 边距
        selectedIndex = iconCount <= 0 ? 0 : NanoMax(0, NanoMin(selectedIndex, iconCount - 1));
        size = constraints.constrainSize({constraints.maxWidth, NanoExtent(iconSize + 10)});
    }

    void paintWorker(Canvas* canvas, Point absPos) override {
        if (iconCount <= 0 || !icons || size.width <= 0 || size.height <= 0)
            return;
        selectedIndex = NanoMax(0, NanoMin(selectedIndex, iconCount - 1));
        ScopedClip clip(canvas, {absPos.x, absPos.y,
                                 NanoCoordinate(static_cast<int32_t>(absPos.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(absPos.y) + size.height)});
        // --- 核心动画逻辑 ---
        // 目标：让选中的图标处于屏幕正中间
        // 计算公式：屏幕中心 - (图标序号 * 间距) - 半个图标宽
        float targetX = (size.width / 2) - (selectedIndex * (iconSize + spacing)) - (iconSize / 2);

        // 缓动平滑滑动
        currentScrollX += (targetX - currentScrollX) * 0.2f;

        if (std::abs(targetX - currentScrollX) > 0.5f) {
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }

        // 绘制所有图标
        for (int i = 0; i < iconCount; i++) {
            // 计算每个图标相对于菜单起始点的 X 坐标
            int16_t drawX =
                NanoCoordinate(static_cast<int32_t>(absPos.x) +
                               static_cast<int32_t>(currentScrollX) + i * (iconSize + spacing));

            // 基础裁剪：只在屏幕横向范围内画
            if (drawX + iconSize < absPos.x || drawX > absPos.x + size.width || !icons[i])
                continue;

            if (i == selectedIndex) {
                // 选中的图标：画一个高亮框，并稍微大一点或加个底座
                canvas->drawRoundRect(drawX - 2, absPos.y - 2, iconSize + 4, iconSize + 4, 3,
                                      C_WHITE);
                // 绘制图标（白色）
                canvas->drawBitmap(drawX, absPos.y, icons[i], iconSize, iconSize, C_WHITE);
            } else {
                // 未选中：直接画图标，可以考虑用简单的点阵表示
                canvas->drawBitmap(drawX, absPos.y, icons[i], iconSize, iconSize, C_WHITE);
            }
        }
    }
};

class ImageMenu : public Widget {
   public:
    const uint8_t** icons;
    int count;
    ImageMenu(const uint8_t** i, int c) : icons(i), count(c) {}
    RenderObject* createRenderObject() override { return new RenderImageMenu(icons, count); }
};

#endif
