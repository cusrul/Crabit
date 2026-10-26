#ifndef RENDERING_RENDER_OBJECT_H
#define RENDERING_RENDER_OBJECT_H

#include "../core/geometry.h"
#include "../hal/canvas.h"
#include "../hal/input.h"

typedef UIEvent Event;

class RenderObject;
typedef void (*RenderObjectVisitor)(RenderObject* child);

#include "../core/memory_pool.h"
#include "../core/embedded_vector.h"

class RenderObject {
   public:
    // 渲染节点从专属内存池分配。
    void* operator new(size_t size) {
        return NanoUI::MemoryPool::instance().allocate(size, "UI_Render");
    }
    void operator delete(void* ptr, size_t size) {
        NanoUI::MemoryPool::instance().deallocate(ptr, size, "UI_Render");
    }

    RenderObject* parent = nullptr;
    BoxConstraints constraints;
    Size size = {0, 0};
    Point pos = {0, 0};
    bool needsLayout = true;
    bool hasFocus = false;
    int flex = 0;
    int16_t preferredMinSize = 0;

    virtual ~RenderObject() {}

    virtual void visitChildren(RenderObjectVisitor visitor) { (void)visitor; }

    void layout(BoxConstraints k) {
        if (needsLayout || k.minWidth != constraints.minWidth ||
            k.maxWidth != constraints.maxWidth || k.minHeight != constraints.minHeight ||
            k.maxHeight != constraints.maxHeight) {
            constraints = k;
            performLayout();
            needsLayout = false;
        }
    }

    virtual void performLayout() = 0;

    void paint(Canvas* canvas, Point offset) {
        Point absPos = {NanoCoordinate((int32_t)offset.x + pos.x),
                        NanoCoordinate((int32_t)offset.y + pos.y)};
        if (hasFocus)
            reportToSystem(absPos);
        paintWorker(canvas, absPos);
    }

    virtual void reportToSystem(Point absPos);
    virtual void paintWorker(Canvas* canvas, Point absolutePos) = 0;
    virtual bool handleEvent(Event e) {
        (void)e;
        return false;
    }
    virtual void onTap() {}
    virtual bool canFocus() { return false; }
    virtual int16_t getRadius() { return 0; }  // 默认无圆角

    void markNeedsLayout() {
        needsLayout = true;
        if (parent)
            parent->markNeedsLayout();
    }
};

#endif
