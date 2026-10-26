#pragma once

#include <NanoUI.h>

// Invert the composed child and its background together. Buttons retain the
// normal WindowManager focus treatment through the forwarded corner radius.
class RenderInvertedSurface : public RenderSingleChildBox {
    int16_t radius;

   public:
    RenderInvertedSurface(RenderObject* content, int16_t cornerRadius)
        : RenderSingleChildBox(content), radius(cornerRadius) {}
    void performLayout() override {
        if (child) {
            child->layout(constraints);
            child->pos = {0, 0};
        }
        size = constraints.constrainSize(child ? child->size : Size{0, 0});
    }
    int16_t getRadius() override { return radius; }
    void paintWorker(Canvas* canvas, Point origin) override {
        RenderSingleChildBox::paintWorker(canvas, origin);
        canvas->fillRoundRect(origin.x, origin.y, size.width, size.height, radius, C_INVERT);
    }
};

class InvertedSurface : public SingleChildWidget {
    int16_t radius;

   public:
    explicit InvertedSurface(Widget* content, int16_t cornerRadius = 0)
        : SingleChildWidget(content), radius(cornerRadius) {}
    RenderObject* createRenderObject() override {
        return new RenderInvertedSurface(child ? child->createRenderObject() : nullptr, radius);
    }
};
