#ifndef WIDGETS_TEXT_H
#define WIDGETS_TEXT_H
#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../hal/hal_time.h"
#include <string.h>
#include "../core/window_manager.h"

// =========================================================
// RenderText: 基础文本显示 (支持多源混合)
// =========================================================
class RenderText : public RenderObject {
   public:
    const char* text;
    uint8_t textSize;
    uint8_t fontId;
    bool scrollEnabled;

    float scrollPos = 0;
    uint32_t lastFrameTime = 0;
    int16_t fullTextWidth = 0;

    RenderText(const char* t, uint8_t s, bool scroll, uint8_t fid = 0)
        : text(t), textSize(s), fontId(fid), scrollEnabled(scroll) {
        lastFrameTime = hal_get_ticks();
    }

    bool canFocus() override { return false; }

    void performLayout() override {
        fullTextWidth = get_utf8_string_width(text, textSize, fontId);
        int16_t w = constraints.maxWidth;
        if (!scrollEnabled || fullTextWidth < constraints.maxWidth) {
            w = fullTextWidth;
        }
        size = constraints.constrainSize({w, (int16_t)textSize});
    }

    void paintWorker(Canvas* canvas, Point absPos) override {
        if (!scrollEnabled || fullTextWidth <= size.width) {
            canvas->drawText(absPos.x, absPos.y, text, textSize, false, fontId);
            return;
        }

        uint32_t now = hal_get_ticks();
        float dt = (now - lastFrameTime) / 1000.0f;
        lastFrameTime = now;
        if (dt < 0.5f)
            scrollPos += 30.0f * dt;
        if (scrollPos > fullTextWidth + 20)
            scrollPos = -size.width;

        canvas->drawTextInRect(absPos.x, absPos.y, size.width, size.height, text,
                               (int16_t)scrollPos, textSize, fontId);
        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }
};

class Text : public Widget {
   public:
    const char* content;
    uint8_t size;
    uint8_t fontId;
    bool scroll;

    Text(const char* c, uint8_t s = 12, bool scr = false, uint8_t f = 0)
        : content(c), size(s), fontId(f), scroll(scr) {}

    RenderObject* createRenderObject() override {
        return new RenderText(content, size, scroll, fontId);
    }
};

// =========================================================
// MarqueeText: 横向滚动文本 (重命名防止冲突)
// =========================================================
class MarqueeText : public Widget {
   public:
    const char* content;
    uint8_t size;
    uint8_t fontId;
    MarqueeText(const char* c, uint8_t s = 12, uint8_t f = 0) : content(c), size(s), fontId(f) {}
    RenderObject* createRenderObject() override {
        return new RenderText(content, size, true, fontId);
    }
};

#endif
