#ifndef WIDGETS_IMAGE_H
#define WIDGETS_IMAGE_H
#include "../framework/widget.h"
#include "../rendering/render_object.h"

class RenderImage : public RenderObject {
   public:
    const uint8_t* bitmap;
    int16_t w, h;
    RenderImage(const uint8_t* b, int16_t width, int16_t height) : bitmap(b), w(width), h(height) {}

    void performLayout() override {
        size = (!bitmap || w <= 0 || h <= 0) ? constraints.constrainSize({0, 0})
                                             : constraints.constrainSize({w, h});
    }

    void paintWorker(Canvas* canvas, Point p) override {
        canvas->drawBitmap(p.x, p.y, bitmap, w, h, C_WHITE);
    }
};

class Image : public Widget {
   public:
    const uint8_t* b;
    int16_t w, h;
    Image(const uint8_t* bit, int16_t width, int16_t height) : b(bit), w(width), h(height) {}
    RenderObject* createRenderObject() override { return new RenderImage(b, w, h); }
};

#endif
