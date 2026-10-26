#ifndef WIDGETS_BASIC_H
#define WIDGETS_BASIC_H
#include "../framework/widget.h"
#include "../rendering/render_object.h"

// --- Expanded ---
class Expanded : public SingleChildWidget {
   public:
    int flexValue;
    Expanded(Widget* child, int f = 1) : SingleChildWidget(child), flexValue(f) {}

    RenderObject* createRenderObject() override {
        if (!child)
            return nullptr;
        RenderObject* ro = child->createRenderObject();
        ro->flex = flexValue;
        return ro;
    }
};

// --- ProgressBar: 修复版 (支持自定义高度) ---
class RenderProgressBar : public RenderObject {
   public:
    float progress;
    int16_t width;
    int16_t height;  // 新增高度字段

    RenderProgressBar(float p, int16_t w, int16_t h) : progress(p), width(w), height(h) {}

    void performLayout() override { size = constraints.constrainSize({width, height}); }

    void paintWorker(Canvas* canvas, Point p) override {
        if (size.width <= 0 || size.height <= 0)
            return;
        // 画外框
        canvas->drawRect(p.x, p.y, size.width, size.height, C_WHITE);

        // 画内充 (留出 1px 边框)
        int16_t innerW = size.width > 2 ? (int16_t)(size.width - 2) : 0;
        int16_t innerH = size.height > 2 ? (int16_t)(size.height - 2) : 0;
        int16_t fillW = (int16_t)(innerW * progress);
        if (fillW < 0)
            fillW = 0;
        if (fillW > innerW)
            fillW = innerW;

        if (fillW > 0 && innerH > 0) {
            canvas->fillRect(p.x + 1, p.y + 1, fillW, innerH, C_WHITE);
        }
    }
};

class ProgressBar : public Widget {
   public:
    float value;
    int16_t width;
    int16_t height;

    // 【修正】构造函数增加 height 参数，默认值 6
    ProgressBar(float v, int16_t w = 100, int16_t h = 6) : value(v), width(w), height(h) {}

    RenderObject* createRenderObject() override {
        return new RenderProgressBar(value, width, height);
    }
};

// --- SizedBox & Spacer ---
class RenderSizedBox : public RenderObject {
   public:
    int16_t w, h;
    RenderSizedBox(int16_t width, int16_t height) : w(width), h(height) {}

    bool canFocus() override { return false; }

    void performLayout() override {
        int16_t finalW = w;
        int16_t finalH = h;

        // Flex 布局核心：如果父级通过 minWidth 传达了 flex 宽度，必须服从
        if (finalW < constraints.minWidth)
            finalW = constraints.minWidth;
        if (finalH < constraints.minHeight)
            finalH = constraints.minHeight;

        // 安全检查：不能撑爆父容器
        if (finalW > constraints.maxWidth)
            finalW = constraints.maxWidth;
        if (finalH > constraints.maxHeight)
            finalH = constraints.maxHeight;

        size = {finalW, finalH};
    }

    void paintWorker(Canvas* canvas, Point p) {}
};

class SizedBox : public Widget {
   public:
    int16_t w, h;
    SizedBox(int16_t width = 0, int16_t height = 0) : w(width), h(height) {}
    RenderObject* createRenderObject() override { return new RenderSizedBox(w, h); }
};

class Spacer : public Widget {
   public:
    int flex;
    int16_t minSize;
    Spacer(int f = 1, int16_t min_s = 0) : flex(f), minSize(min_s) {}
    RenderObject* createRenderObject() override {
        RenderObject* ro = new RenderSizedBox(0, 0);
        ro->flex = flex;
        ro->preferredMinSize = minSize;
        return ro;
    }
};

// --- Fill (通用填充器) ---
class Fill : public SingleChildWidget {
   public:
    int flex;
    Fill(Widget* w = nullptr, int f = 1) : SingleChildWidget(w), flex(f) {}

    RenderObject* createRenderObject() override {
        RenderObject* ro = child ? child->createRenderObject() : new RenderSizedBox(0, 0);
        ro->flex = flex;
        return ro;
    }
};

#endif
