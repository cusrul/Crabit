#ifndef WIDGETS_FLEX_H
#define WIDGETS_FLEX_H

#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/embedded_vector.h"
#include "viewport.h"

// One retained-render algorithm, two direction-specific entry points. This
// main/cross axes share the same sizing, minimum-space and overflow rules.
class RenderFlex : public RenderObject {
   protected:
    virtual bool isHorizontal() const = 0;

   public:
    ~RenderFlex() override {
        for (auto c : children) {
            if (c)
                delete c;
        }
        children.clear();
    }
    NanoUI::EmbeddedVector<RenderObject*> children;
    int32_t contentMainExtent = 0;
    int32_t overflowMainExtent = 0;
    void visitChildren(RenderObjectVisitor visitor) override {
        for (auto c : children)
            visitor(c);
    }

    void performLayout() override {
        const bool horizontal = isHorizontal();
        const int16_t mainLimit = horizontal ? constraints.maxWidth : constraints.maxHeight;
        int64_t totalFlex = 0;
        int32_t rigidMain = 0, maxCross = 0, minFlexMain = 0;

        for (auto child : children) {
            if (child->flex <= 0) {
                child->layout(BoxConstraints::loose(constraints.maxSize()));
                rigidMain += horizontal ? child->size.width : child->size.height;
                const int16_t cross = horizontal ? child->size.height : child->size.width;
                if (cross > maxCross)
                    maxCross = cross;
            } else {
                totalFlex += child->flex;
                minFlexMain += NanoMax<int16_t>(0, child->preferredMinSize);
            }
        }

        const bool bounded =
            horizontal ? constraints.hasBoundedWidth() : constraints.hasBoundedHeight();
        const int32_t budget = NanoMax<int32_t>(minFlexMain, mainLimit - rigidMain);
        int32_t activeBudget = budget;
        int64_t activeWeight = totalFlex;
        // Pin children whose minimum exceeds their weighted share, then
        // redistribute the remaining space. Recomputing the active set needs
        // no scratch allocations and reaches a fixed point in at most N passes.
        if (bounded && totalFlex > 0) {
            for (size_t pass = 0; pass < children.size(); ++pass) {
                int32_t nextBudget = budget;
                int64_t nextWeight = totalFlex;
                for (auto child : children) {
                    if (child->flex <= 0)
                        continue;
                    const int32_t minimum = NanoMax<int16_t>(0, child->preferredMinSize);
                    if ((int64_t)minimum * activeWeight > (int64_t)activeBudget * child->flex) {
                        nextBudget -= minimum;
                        nextWeight -= child->flex;
                    }
                }
                if (nextBudget == activeBudget && nextWeight == activeWeight)
                    break;
                activeBudget = nextBudget;
                activeWeight = nextWeight;
            }
        }
        const int32_t ratioBudget = activeBudget;
        const int64_t ratioWeight = activeWeight;
        int32_t cursor = 0;

        for (auto child : children) {
            if (child->flex > 0) {
                int32_t extent = NanoMax<int16_t>(0, child->preferredMinSize);
                const bool pinned =
                    (int64_t)extent * ratioWeight > (int64_t)ratioBudget * child->flex;
                if (bounded && !pinned && activeWeight > 0) {
                    extent =
                        static_cast<int32_t>((int64_t)activeBudget * child->flex / activeWeight);
                    activeBudget -= extent;
                    activeWeight -= child->flex;
                }
                const int16_t flexExtent = NanoExtent(extent);
                const BoxConstraints childConstraints =
                    horizontal ? BoxConstraints{flexExtent, flexExtent, 0, constraints.maxHeight}
                               : BoxConstraints{0, constraints.maxWidth, flexExtent, flexExtent};
                child->layout(childConstraints);
            }

            child->pos =
                horizontal ? Point{NanoCoordinate(cursor), 0} : Point{0, NanoCoordinate(cursor)};
            const int16_t cross = horizontal ? child->size.height : child->size.width;
            if (cross > maxCross)
                maxCross = cross;
            cursor += horizontal ? child->size.width : child->size.height;
        }

        contentMainExtent = cursor;
        size =
            constraints.constrainSize(horizontal ? Size{NanoExtent(cursor), NanoExtent(maxCross)}
                                                 : Size{NanoExtent(maxCross), NanoExtent(cursor)});
        overflowMainExtent = NanoMax<int32_t>(0, cursor - (horizontal ? size.width : size.height));
        if (horizontal) {
            for (auto child : children) {
                child->pos.y = NanoExtent((int32_t)size.height - child->size.height) / 2;
            }
        }
    }

    void paintWorker(Canvas* c, Point p) override {
        for (auto k : children)
            k->paint(c, p);
    }
};

class RenderRow : public RenderFlex {
   protected:
    bool isHorizontal() const override { return true; }
};

class Row : public MultiChildWidget {
   public:
    template <typename Container>
    Row(const Container& c) : MultiChildWidget(c) {}
    Row(std::initializer_list<Widget*> c) : MultiChildWidget(c) {}
    RenderObject* createRenderObject() override {
        RenderRow* ro = new RenderRow();
        for (auto w : children) {
            RenderObject* r = w->createRenderObject();
            r->parent = ro;
            ro->children.push_back(r);
        }
        return ro;
    }
};

class RenderColumn : public RenderFlex {
   protected:
    bool isHorizontal() const override { return false; }
};

class Column : public MultiChildWidget {
   public:
    template <typename Container>
    Column(const Container& c) : MultiChildWidget(c) {}
    Column(std::initializer_list<Widget*> c) : MultiChildWidget(c) {}
    RenderObject* createRenderObject() override {
        RenderColumn* ro = new RenderColumn();
        for (auto w : children) {
            RenderObject* r = w->createRenderObject();
            r->parent = ro;
            ro->children.push_back(r);
        }
        return ro;
    }
};

// =========================================================
// RenderScrollColumn：带防抖与弹簧过渡的滚动列。
// =========================================================
#include "../core/animator.h"

class RenderScrollColumn : public RenderColumn {
   private:
    SpringAnimator scrollAnim;
    float targetScrollOffset = 0;
    int32_t totalContentHeight = 0;

   public:
    RenderScrollColumn() : scrollAnim(0, 0.15f, 0.7f) {}

    void performLayout() override {
        int16_t maxW = 0;
        int32_t yPos = 0;
        int16_t contentMaxWidth = constraints.maxWidth;

        for (auto child : children) {
            child->layout(BoxConstraints::loose({contentMaxWidth, BoxConstraints::Unbounded}));
            child->pos = {0, NanoCoordinate(yPos)};
            yPos += child->size.height;
            if (child->size.width > maxW)
                maxW = child->size.width;
        }
        totalContentHeight = yPos;
        size = ViewportSize(constraints, {maxW, NanoExtent(yPos)});
        for (auto child : children)
            child->pos.x = NanoExtent(static_cast<int32_t>(size.width) - child->size.width) / 2;
        const int32_t maxScroll = NanoMax<int32_t>(0, totalContentHeight - size.height);
        targetScrollOffset = NanoMax<float>(0, NanoMin<float>(targetScrollOffset, maxScroll));
        // After content/viewport shrink, an old offset must not leave a blank
        // viewport even when focus no longer belongs to this column.
        if (scrollAnim.value < 0 || scrollAnim.value > maxScroll)
            scrollAnim.jumpTo(targetScrollOffset);
        scrollAnim.set(targetScrollOffset);
    }

    void paintWorker(Canvas* canvas, Point absPos) override {
        int16_t viewportH = size.height;
        ScopedClip clip(canvas, ViewportRect(absPos, size));
        RenderObject* globalFocus =
            WindowManager::instance ? WindowManager::instance->getFocusedRenderObject() : nullptr;

        float tY = 0;
        bool found = false;
        if (globalFocus) {
            RenderObject* p = globalFocus;
            while (p && p != this) {
                tY += p->pos.y;
                p = p->parent;
            }
            if (p == this)
                found = true;
        }

        if (found) {
            int32_t maxScroll = NanoMax<int32_t>(0, totalContentHeight - viewportH);
            if (tY < targetScrollOffset + 10)
                targetScrollOffset = tY - 10;
            else if (tY + globalFocus->size.height > targetScrollOffset + viewportH - 10)
                targetScrollOffset = tY + globalFocus->size.height - viewportH + 10;

            if (targetScrollOffset < 0)
                targetScrollOffset = 0;
            if (targetScrollOffset > maxScroll)
                targetScrollOffset = maxScroll;
            scrollAnim.set(targetScrollOffset);
        }

        float currentScroll = scrollAnim.update();
        int16_t intScroll = static_cast<int16_t>(currentScroll);

        // 只绘制子组件，不绘滚动条，不裁剪
        for (auto child : children) {
            int16_t drawY = child->pos.y - intScroll;
            if (drawY + child->size.height > 0 && drawY < viewportH) {
                child->paint(canvas, {absPos.x, NanoCoordinate((int32_t)absPos.y - intScroll)});
            }
        }

        if (!scrollAnim.isStable()) {
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }
    }
};

class ScrollColumn : public Column {
   public:
    ScrollColumn(std::initializer_list<Widget*> children) : Column(children) {}
    template <typename Container>
    ScrollColumn(const Container& children) : Column(children) {}
    RenderObject* createRenderObject() override {
        RenderScrollColumn* ro = new RenderScrollColumn();
        for (auto w : children) {
            RenderObject* r = w->createRenderObject();
            r->parent = ro;
            ro->children.push_back(r);
        }
        return ro;
    }
};

#endif
