#ifndef NANOUI_VIEWPORT_H
#define NANOUI_VIEWPORT_H

#include "layout.h"

inline Rect ViewportRect(Point origin, Size size) {
    return {origin.x, origin.y, NanoCoordinate((int32_t)origin.x + size.width),
            NanoCoordinate((int32_t)origin.y + size.height)};
}

inline Size ViewportSize(BoxConstraints bounds, Size content) {
    return bounds.constrainSize({bounds.hasBoundedWidth() ? bounds.maxWidth : content.width,
                                 bounds.hasBoundedHeight() ? bounds.maxHeight : content.height});
}

enum class ViewportAxis { Horizontal, Vertical };

// Content layout remains independent of the viewport and display offset.
class RenderViewport : public RenderSingleChildBox {
   public:
    ViewportAxis axis;
    Point scrollOffset{0, 0};
    RenderViewport(RenderObject* content, ViewportAxis direction)
        : RenderSingleChildBox(content), axis(direction) {}
    void performLayout() override {
        BoxConstraints inner = constraints.loosen();
        if (axis == ViewportAxis::Vertical)
            inner.maxHeight = BoxConstraints::Unbounded;
        else
            inner.maxWidth = BoxConstraints::Unbounded;
        if (child) {
            child->layout(inner);
            child->pos = {0, 0};
        }
        size = ViewportSize(constraints, child ? child->size : Size{0, 0});
    }
    void paintWorker(Canvas* c, Point p) override {
        ScopedClip clip(c, ViewportRect(p, size));
        if (child)
            child->paint(c, {NanoCoordinate((int32_t)p.x - scrollOffset.x),
                             NanoCoordinate((int32_t)p.y - scrollOffset.y)});
    }
};

class Viewport : public SingleChildWidget {
   public:
    ViewportAxis axis;
    Viewport(Widget* content, ViewportAxis direction = ViewportAxis::Vertical)
        : SingleChildWidget(content), axis(direction) {}
    RenderObject* createRenderObject() override {
        return new RenderViewport(child ? child->createRenderObject() : nullptr, axis);
    }
};

#endif
