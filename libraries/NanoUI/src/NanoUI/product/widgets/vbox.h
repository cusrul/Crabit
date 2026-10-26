#ifndef WIDGETS_VBOX_H
#define WIDGETS_VBOX_H

#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/embedded_vector.h"

// =========================================================
// VBox: 绝对自适应的垂直布局 (不做滚动，不贪心)
// =========================================================
class RenderVBox : public RenderObject {
   public:
    NanoUI::EmbeddedVector<RenderObject*> children;

    // VBox 销毁时释放其所有子组件。
    ~RenderVBox() override {
        for (auto c : children) {
            if (c)
                delete c;
        }
        children.clear();
    }

    void visitChildren(RenderObjectVisitor visitor) override {
        for (auto c : children)
            visitor(c);
    }

    bool handleEvent(Event e) override {
        for (auto c : children) {
            if (c->handleEvent(e))
                return true;
        }
        return false;
    }

    void performLayout() override {
        int16_t maxW = 0;
        int32_t currentY = 0;

        for (auto child : children) {
            // 关键：告诉孩子 "你可以用任意高度"，这样它就会算出真实高度
            // 宽度限制为父容器宽度
            child->layout({0, constraints.maxWidth, 0, BoxConstraints::Unbounded});

            child->pos = {0, NanoCoordinate(currentY)};

            if (child->size.width > maxW)
                maxW = child->size.width;
            currentY += child->size.height;
        }

        // 核心：我的高度 = 所有孩子高度之和
        // 绝不使用 constraints.maxHeight (那会导致撑满)
        size = constraints.constrainSize(
            {constraints.hasBoundedWidth() ? constraints.maxWidth : maxW, NanoExtent(currentY)});
        for (auto child : children)
            child->pos.x = NanoExtent((int32_t)size.width - child->size.width) / 2;
    }

    void paintWorker(Canvas* canvas, Point p) override {
        for (auto child : children) {
            child->paint(canvas, p);
        }
    }
};

class VBox : public MultiChildWidget {
   public:
    VBox(std::initializer_list<Widget*> children) : MultiChildWidget(children) {}
    template <typename Container>
    VBox(const Container& children) : MultiChildWidget(children) {}
    RenderObject* createRenderObject() override {
        RenderVBox* ro = new RenderVBox();
        for (auto w : children) {
            RenderObject* r = w->createRenderObject();
            r->parent = ro;
            ro->children.push_back(r);
        }
        return ro;
    }
};

#endif
