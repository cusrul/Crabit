#ifndef NANOUI_WIDGETS_LAYOUT_H
#define NANOUI_WIDGETS_LAYOUT_H

#include "../framework/widget.h"

// Proxies own their render child independently of temporary Widget configs.
// They are not focus scopes: discovery continues through visitChildren().
class RenderSingleChildBox : public RenderObject {
   public:
    RenderObject* child = nullptr;
    explicit RenderSingleChildBox(RenderObject* content = nullptr) : child(content) {
        if (child)
            child->parent = this;
    }
    ~RenderSingleChildBox() override { delete child; }
    void visitChildren(RenderObjectVisitor visitor) override {
        if (child)
            visitor(child);
    }
    void paintWorker(Canvas* canvas, Point origin) override {
        if (child)
            child->paint(canvas, origin);
    }
    int16_t getRadius() override { return child ? child->getRadius() : 0; }
};

class RenderConstrainedBox : public RenderSingleChildBox {
   public:
    BoxConstraints additionalConstraints;
    RenderConstrainedBox(RenderObject* content, BoxConstraints extra)
        : RenderSingleChildBox(content), additionalConstraints(extra) {}
    void performLayout() override {
        const BoxConstraints effective = additionalConstraints.enforce(constraints);
        if (child) {
            child->layout(effective);
            child->pos = {0, 0};
        }
        size = effective.constrainSize(child ? child->size : Size{0, 0});
    }
};

class ConstrainedBox : public SingleChildWidget {
   public:
    BoxConstraints additionalConstraints;
    ConstrainedBox(Widget* content, BoxConstraints extra)
        : SingleChildWidget(content), additionalConstraints(extra) {}
    RenderObject* createRenderObject() override {
        return new RenderConstrainedBox(child ? child->createRenderObject() : nullptr,
                                        additionalConstraints);
    }
};

class RenderPadding : public RenderSingleChildBox {
   public:
    Insets insets;
    RenderPadding(RenderObject* content, Insets value)
        : RenderSingleChildBox(content), insets(value) {}
    void performLayout() override {
        if (child) {
            child->layout(constraints.deflate(insets));
            child->pos = {insets.left, insets.top};
        }
        const Size content = child ? child->size : Size{0, 0};
        size = constraints.constrainSize({NanoExtent(content.width + insets.horizontal()),
                                          NanoExtent(content.height + insets.vertical())});
    }
};

class Padding : public SingleChildWidget {
   public:
    Insets insets;
    Padding(Widget* content, Insets value) : SingleChildWidget(content), insets(value) {}
    RenderObject* createRenderObject() override {
        return new RenderPadding(child ? child->createRenderObject() : nullptr, insets);
    }
};

enum class Alignment {
    TopLeft,
    TopCenter,
    TopRight,
    CenterLeft,
    Center,
    CenterRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

inline Point AlignmentOffset(Alignment alignment, Size outer, Size inner) {
    const int16_t dx = NanoExtent((int32_t)outer.width - inner.width);
    const int16_t dy = NanoExtent((int32_t)outer.height - inner.height);
    const int horizontal = static_cast<int>(alignment) % 3;
    const int vertical = static_cast<int>(alignment) / 3;
    return {static_cast<int16_t>(horizontal == 0   ? 0
                                 : horizontal == 1 ? dx / 2
                                                   : dx),
            static_cast<int16_t>(vertical == 0   ? 0
                                 : vertical == 1 ? dy / 2
                                                 : dy)};
}

class RenderAlign : public RenderSingleChildBox {
   public:
    Alignment alignment;
    bool expandWidth, expandHeight;
    RenderAlign(RenderObject* content, Alignment value, bool fillWidth = false,
                bool fillHeight = false)
        : RenderSingleChildBox(content),
          alignment(value),
          expandWidth(fillWidth),
          expandHeight(fillHeight) {}
    void performLayout() override {
        if (child)
            child->layout(constraints.loosen());
        const Size content = child ? child->size : Size{0, 0};
        size = constraints.constrainSize(
            {expandWidth && constraints.hasBoundedWidth() ? constraints.maxWidth : content.width,
             expandHeight && constraints.hasBoundedHeight() ? constraints.maxHeight
                                                            : content.height});
        if (child) {
            // Keep the leading edge of oversized legacy content visible.
            child->pos = AlignmentOffset(alignment, size, content);
        }
    }
};

class Align : public SingleChildWidget {
   public:
    Alignment alignment;
    bool expandWidth, expandHeight;
    Align(Widget* content, Alignment value = Alignment::Center, bool fillWidth = false,
          bool fillHeight = false)
        : SingleChildWidget(content),
          alignment(value),
          expandWidth(fillWidth),
          expandHeight(fillHeight) {}
    RenderObject* createRenderObject() override {
        return new RenderAlign(child ? child->createRenderObject() : nullptr, alignment,
                               expandWidth, expandHeight);
    }
};

class RenderDecoratedBox : public RenderSingleChildBox {
   public:
    bool hasBorder;
    int16_t radius;
    RenderDecoratedBox(RenderObject* content, bool border = true, int16_t r = 0)
        : RenderSingleChildBox(content), hasBorder(border), radius(r) {}
    void performLayout() override {
        if (child) {
            child->layout(constraints);
            child->pos = {0, 0};
        }
        size = constraints.constrainSize(child ? child->size : Size{0, 0});
    }
    void paintWorker(Canvas* canvas, Point origin) override {
        // Border draws inside the box; no implicit padding or clipping.
        if (hasBorder && size.width > 0 && size.height > 0) {
            if (radius > 0)
                canvas->drawRoundRect(origin.x, origin.y, size.width, size.height, radius, C_WHITE);
            else
                canvas->drawRect(origin.x, origin.y, size.width, size.height, C_WHITE);
        }
        RenderSingleChildBox::paintWorker(canvas, origin);
    }
    int16_t getRadius() override { return radius; }
};

class DecoratedBox : public SingleChildWidget {
   public:
    bool hasBorder;
    int16_t radius;
    DecoratedBox(Widget* content, bool border = true, int16_t r = 0)
        : SingleChildWidget(content), hasBorder(border), radius(r) {}
    RenderObject* createRenderObject() override {
        return new RenderDecoratedBox(child ? child->createRenderObject() : nullptr, hasBorder,
                                      radius);
    }
};

// Clipping is explicit, not an implicit side effect of every Container.
class RenderClipRect : public RenderSingleChildBox {
   public:
    explicit RenderClipRect(RenderObject* content) : RenderSingleChildBox(content) {}
    void performLayout() override {
        if (child) {
            child->layout(constraints);
            child->pos = {0, 0};
        }
        size = constraints.constrainSize(child ? child->size : Size{0, 0});
    }
    void paintWorker(Canvas* canvas, Point origin) override {
        ScopedClip clip(canvas, {origin.x, origin.y,
                                 NanoCoordinate(static_cast<int32_t>(origin.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(origin.y) + size.height)});
        RenderSingleChildBox::paintWorker(canvas, origin);
    }
};

class ClipRect : public SingleChildWidget {
   public:
    explicit ClipRect(Widget* content) : SingleChildWidget(content) {}
    RenderObject* createRenderObject() override {
        return new RenderClipRect(child ? child->createRenderObject() : nullptr);
    }
};

class RenderStack : public RenderObject {
   public:
    NanoUI::EmbeddedVector<RenderObject*> children;
    Alignment alignment;
    explicit RenderStack(Alignment value) : alignment(value) {}
    ~RenderStack() override {
        for (auto child : children) {
            child->parent = nullptr;
            delete child;
        }
    }
    void visitChildren(RenderObjectVisitor visitor) override {
        for (auto child : children)
            visitor(child);
    }
    void performLayout() override {
        Size content{0, 0};
        for (auto child : children) {
            child->layout(constraints.loosen());
            content.width = NanoMax(content.width, child->size.width);
            content.height = NanoMax(content.height, child->size.height);
        }
        size = constraints.constrainSize(content);
        for (auto child : children)
            child->pos = AlignmentOffset(alignment, size, child->size);
    }
    void paintWorker(Canvas* c, Point p) override {
        for (auto child : children)
            child->paint(c, p);
    }
};

class Stack : public MultiChildWidget {
   public:
    Alignment alignment;
    Stack(std::initializer_list<Widget*> content, Alignment value = Alignment::TopLeft)
        : MultiChildWidget(content), alignment(value) {}
    template <typename Container>
    Stack(const Container& content, Alignment value = Alignment::TopLeft)
        : MultiChildWidget(content), alignment(value) {}
    RenderObject* createRenderObject() override {
        RenderStack* root = new RenderStack(alignment);
        for (auto widget : children) {
            RenderObject* node = widget->createRenderObject();
            if (!node)
                continue;
            node->parent = root;
#if NANOUI_DYNAMIC_STORAGE
            root->children.push_back(node);
#else
            if (!root->children.push_back(node)) {
                delete node;
            }
#endif
        }
        return root;
    }
};

#endif
