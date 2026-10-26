#ifndef WIDGETS_CONTAINER_H
#define WIDGETS_CONTAINER_H
#include "layout.h"

// Keep the public content pointer and legacy constructor. The private render
// composition borrows that content; only this facade owns the public child.
class RenderContainer : public RenderObject {
    friend class Container;
    RenderConstrainedBox* composition;
    RenderDecoratedBox* decoration;
    RenderPadding* padded;
    RenderAlign* aligned;

    void syncContent() {
        // A replacement allocation can reuse the deleted child's address.
        // Parent/dirty checks must not mistake that new node for cached content.
        if (aligned->child != child ||
            (child && (child->parent != aligned || child->needsLayout))) {
            // Do not delete the old pointer: legacy callers may already have
            // deleted it before assigning replacement content.
            aligned->child = child;
            if (child)
                child->parent = aligned;
            aligned->markNeedsLayout();
        }
    }

   public:
    RenderObject* child = nullptr;
    int16_t width, height;
    int16_t padding;
    bool hasBorder;
    int16_t radius;

    RenderContainer(int16_t w, int16_t h, int16_t p, bool border, int16_t r)
        : width(w), height(h), padding(p), hasBorder(border), radius(r) {
        aligned = new RenderAlign(nullptr, Alignment::Center);
        padded = new RenderPadding(aligned, Insets::all(padding));
        decoration = new RenderDecoratedBox(padded, hasBorder, radius);
        composition = new RenderConstrainedBox(decoration, {0, 32767, 0, 32767});
        composition->parent = this;
    }

    ~RenderContainer() override {
        aligned->child = nullptr;  // content is owned by the facade, not twice
        if (child)
            child->parent = nullptr;  // no upward link into deleted proxies
        delete composition;
        delete child;
    }

    void visitChildren(RenderObjectVisitor visitor) override {
        syncContent();
        visitor(composition);
    }

    void performLayout() override {
        syncContent();
        const BoxConstraints nextConstraints = {width > 0 ? width : static_cast<int16_t>(0),
                                                width > 0 ? width : static_cast<int16_t>(32767),
                                                height > 0 ? height : static_cast<int16_t>(0),
                                                height > 0 ? height : static_cast<int16_t>(32767)};
        if (padded->insets.left != padding) {
            padded->insets = Insets::all(padding);
            padded->markNeedsLayout();
        }
        const BoxConstraints old = composition->additionalConstraints;
        if (old.minWidth != nextConstraints.minWidth || old.maxWidth != nextConstraints.maxWidth ||
            old.minHeight != nextConstraints.minHeight ||
            old.maxHeight != nextConstraints.maxHeight) {
            composition->additionalConstraints = nextConstraints;
            composition->markNeedsLayout();
        }
        decoration->hasBorder = hasBorder;
        decoration->radius = radius;
        composition->layout(constraints);
        composition->pos = {0, 0};
        size = composition->size;
    }

    void paintWorker(Canvas* canvas, Point p) override { composition->paint(canvas, p); }

    int16_t getRadius() override { return radius; }
};

class Container : public SingleChildWidget {
   public:
    int16_t width, height, padding, radius;
    bool border;
    Container(Widget* child, int16_t w = 0, int16_t h = 0, int16_t p = 0, bool b = false,
              int16_t r = 0)
        : SingleChildWidget(child), width(w), height(h), padding(p), radius(r), border(b) {}
    RenderObject* createRenderObject() override {
        RenderContainer* ro = new RenderContainer(width, height, padding, border, radius);
        ro->child = child ? child->createRenderObject() : nullptr;
        ro->syncContent();
        return ro;
    }
};
#endif
