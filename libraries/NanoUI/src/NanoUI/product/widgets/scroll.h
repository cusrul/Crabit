#ifndef WIDGETS_SCROLL_H
#define WIDGETS_SCROLL_H

#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/animator.h"
#include "../core/embedded_vector.h"
#include "viewport.h"

class RenderListView : public RenderObject {
   public:
    NanoUI::EmbeddedVector<RenderObject*> children;
    int selectedIndex = 0;

    // 滚动偏移量 (动画)
    SpringAnimator scrollY;
    float targetScrollY = 0;

    // 光标动画
    SpringAnimator cursorY;
    float targetCursorY = 0;
    int16_t cursorH = 0;
    int32_t contentHeight = 0;
    bool firstRun = true;
    bool canFocus() override { return !children.empty(); }
    void visitChildren(RenderObjectVisitor visitor) override {
        for (auto child : children)
            visitor(child);
    }
    // ListView 销毁时释放全部列表子项。
    ~RenderListView() override {
        for (auto c : children) {
            if (c)
                delete c;
        }
        children.clear();
    }
    RenderListView() : scrollY(0, 0.15f, 0.8f), cursorY(0, 0.25f, 0.7f) {}

    void performLayout() override {
        int32_t yOffset = 0;
        int16_t maxW = 0;
        BoxConstraints childConstraints = {0, NanoExtent((int32_t)constraints.maxWidth - 4), 0,
                                           BoxConstraints::Unbounded};
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(children.size()))
            selectedIndex = 0;

        // 1. 布局所有子项，计算总高度
        int idx = 0;
        for (auto child : children) {
            child->layout(childConstraints);

            // 默认左对齐，留一点边距
            child->pos = {2, NanoCoordinate(yOffset)};

            if (idx == selectedIndex) {
                // 计算光标的目标位置 (相对于列表顶部)
                targetCursorY = yOffset;
                cursorH = child->size.height;

                // 【核心滚动逻辑】
                // 如果选中的项跑到了屏幕下面 (yOffset > scrollY + 屏幕高 - 底部留白)
                // 就要把 scrollY 变大 (往下滚)
                int16_t viewHeight = constraints.maxHeight;

                // 目标：让选中的项保持在可视区域内
                // 如果当前项在视口上方，滚上去
                if (yOffset < targetScrollY) {
                    targetScrollY = yOffset;
                }
                // 如果当前项在视口下方，滚下来
                // (yOffset + height) 是项的底部
                // (targetScrollY + viewHeight) 是视口的底部
                else if (yOffset + child->size.height > targetScrollY + viewHeight) {
                    targetScrollY = (yOffset + child->size.height) - viewHeight;
                }
            }

            child->hasFocus = hasFocus && (idx == selectedIndex);

            yOffset += child->size.height;
            if (child->size.width > maxW)
                maxW = child->size.width;
            idx++;
        }

        contentHeight = yOffset;
        size = ViewportSize(constraints, {NanoExtent((int32_t)maxW + 4), NanoExtent(yOffset)});
        targetScrollY = NanoMax<float>(
            0, NanoMin<float>(targetScrollY, NanoMax<int32_t>(0, contentHeight - size.height)));
        // 驱动动画
        scrollY.set(targetScrollY);
        cursorY.set(targetCursorY);

        if (firstRun) {
            scrollY.jumpTo(targetScrollY);
            cursorY.jumpTo(targetCursorY);
            firstRun = false;
        }

        // ListView 占满父容器
    }

    void paintWorker(Canvas* canvas, Point absolutePos) override {
        float curScroll = scrollY.update();
        float curCursor = cursorY.update();

        if (!scrollY.isStable() || !cursorY.isStable()) {
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }

        // 开启裁剪，防止子项溢出可见区。
        ScopedClip clip(canvas, ViewportRect(absolutePos, size));

        // 1. 画光标 (相对于 scrollOffset)
        int16_t drawCursorY = absolutePos.y + (int16_t)(curCursor - curScroll);
        if (drawCursorY > absolutePos.y - cursorH && drawCursorY < absolutePos.y + size.height) {
            canvas->fillRoundRect(absolutePos.x, drawCursorY, size.width, cursorH, 4, C_WHITE);
        }

        // 2. 画子组件
        for (auto child : children) {
            child->hasFocus = hasFocus && child == children[selectedIndex];
            int16_t childDrawY = absolutePos.y + child->pos.y - (int16_t)curScroll;
            if (childDrawY + child->size.height > absolutePos.y &&
                childDrawY < absolutePos.y + size.height) {
                Point drawOffset = {absolutePos.x,
                                    NanoCoordinate((int32_t)absolutePos.y - (int32_t)curScroll)};
                child->paint(canvas, drawOffset);
            }
        }

        // 还原裁剪区
    }

    bool handleEvent(Event e) override {
        // ... (按键逻辑与 Column 类似，只是控制 selectedIndex) ...
        if (children.empty())
            return false;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(children.size()))
            selectedIndex = 0;
        children[selectedIndex]->hasFocus = hasFocus;
        if (children[selectedIndex]->handleEvent(e))
            return true;
        int count = (int)children.size();
        int oldIndex = selectedIndex;

        if (e.type == EVT_DOWN) {
            int next = selectedIndex;
            do {
                next++;
                if (next >= count)
                    next = 0;
                if (children[next]->canFocus()) {
                    selectedIndex = next;
                    break;
                }
            } while (next != selectedIndex);
        } else if (e.type == EVT_UP) {
            int next = selectedIndex;
            do {
                next--;
                if (next < 0)
                    next = count - 1;
                if (children[next]->canFocus()) {
                    selectedIndex = next;
                    break;
                }
            } while (next != selectedIndex);
        } else if (e.type == EVT_OK) {
            if (children[selectedIndex]->canFocus()) {
                // Preserve legacy onTap-only items when handleEvent did not
                // consume OK. A Button consumed above must never fire twice.
                children[selectedIndex]->onTap();
                return true;
            }
        } else {
            return false;
        }

        if (oldIndex != selectedIndex) {
            markNeedsLayout();
            return true;
        }
        return false;
    }
};

class ListView : public MultiChildWidget {
   public:
    ListView(std::initializer_list<Widget*> children) : MultiChildWidget(children) {}
    template <typename Container>
    ListView(const Container& children) : MultiChildWidget(children) {}
    RenderObject* createRenderObject() override {
        RenderListView* ro = new RenderListView();
        for (auto w : children) {
            ro->children.push_back(w->createRenderObject());
            ro->children.back()->parent = ro;
        }
        return ro;
    }
};

#endif
