#ifndef WIDGETS_CONTROLS_H
#define WIDGETS_CONTROLS_H

#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/window_manager.h"
#include "../core/animator.h"
#include "../core/callback.h"
#include <cmath>
#include <string>
#include "../core/text.h"
#include <cstdio>

typedef void (*BoolCallback)(bool value);
typedef void (*FloatCallback)(float value);

// =========================================================
// Switch: 灵动胖胶囊版 (更佳占比，更萌视觉)
// =========================================================
class RenderSwitch : public RenderObject {
   public:
    bool* value;
    NanoUI::Callback<void(bool)> onChanged;
    SpringAnimator anim;

    RenderSwitch(bool* v, NanoUI::Callback<void(bool)> cb)
        : value(v), onChanged(cb), anim(*v ? 1.0f : 0.0f, 0.15f, 0.6f) {
        anim.threshold = 0.01f;
        size = {26, 14};
    }

    bool canFocus() override { return true; }

    bool handleEvent(Event e) override {
        if (hasFocus && e.type == EVT_OK) {
            *value = !(*value);
            anim.set(*value ? 1.0f : 0.0f);
            const auto callback = onChanged;
            if (callback)
                callback(*value);
            return true;
        }
        return false;
    }

    void performLayout() override { size = constraints.constrainSize({26, 14}); }

    void paintWorker(Canvas* canvas, Point p) override {
        float prog = anim.update();
        if (!anim.isStable() && WindowManager::instance)
            WindowManager::instance->markDirty();

        // 1. 绘制 2px 厚度的圆角外框 (通过双层线框嵌套实现视觉上的加粗)
        canvas->drawRoundRect(p.x, p.y, 26, 14, 7, C_WHITE);
        canvas->drawRoundRect(p.x + 1, p.y + 1, 24, 12, 6, C_WHITE);

        // 2. 内部实心球 (尺寸占比更高，视觉更萌)
        // 运动范围精密计算：确保圆球永远停留在加厚内壁边缘，绝不冲出
        int16_t thumbX = (int16_t)(p.x + 7 + prog * 11);
        canvas->fillCircle(thumbX, p.y + 7, 5, C_WHITE);
    }
};

class Switch : public Widget {
   public:
    bool* value;
    NanoUI::Callback<void(bool)> onChanged;
    Switch(bool& v, NanoUI::Callback<void(bool)> cb = nullptr) : value(&v), onChanged(cb) {}
    RenderObject* createRenderObject() override { return new RenderSwitch(value, onChanged); }
};

// =========================================================
// Slider: 纤细丝线进度管 (带丝滑物理动画)
// =========================================================
class RenderSlider : public RenderObject {
   public:
    float* value;
    float maxVal;
    int16_t targetWidth;
    NanoUI::Callback<void(float)> onChanged;
    bool isEditing = false;
    SpringAnimator thumbAnim;  // 控件位置带弹簧过渡

    RenderSlider(float* v, float m, int16_t w, NanoUI::Callback<void(float)> cb)
        : value(v), maxVal(m), targetWidth(w), onChanged(cb), thumbAnim(*v, 0.15f, 0.75f) {
        size = {targetWidth, 8};
    }

    bool canFocus() override { return true; }

    bool handleEvent(Event e) override {
        if (!hasFocus)
            return false;
        if (e.type == EVT_OK) {
            isEditing = !isEditing;
            return true;
        }
        if (isEditing) {
            float step = maxVal / 10.0f;
            if (e.type == EVT_RIGHT || e.type == EVT_UP) {
                *value = fmin(maxVal, *value + step);
                thumbAnim.set(*value);
                const auto callback = onChanged;
                if (callback)
                    callback(*value);
                return true;
            } else if (e.type == EVT_LEFT || e.type == EVT_DOWN) {
                *value = fmax(0.0f, *value - step);
                thumbAnim.set(*value);
                const auto callback = onChanged;
                if (callback)
                    callback(*value);
                return true;
            }
        }
        return false;
    }

    void performLayout() override { size = constraints.constrainSize({targetWidth, 8}); }

    void paintWorker(Canvas* canvas, Point p) override {
        if (size.width <= 0 || size.height <= 0)
            return;
        // 每帧推进动画，否则滑动过程不会重绘
        float curVal = thumbAnim.update();
        if (!thumbAnim.isStable() && WindowManager::instance)
            WindowManager::instance->markDirty();

        // 1. 绘制外槽
        canvas->drawRoundRect(p.x, p.y, size.width, 8, 4, C_WHITE);

        // 2. 内部丝线进度 (基于 curVal 计算，实现平滑移动)
        int16_t maxFillW = size.width > 6 ? (int16_t)(size.width - 6) : 0;
        int16_t fillW = maxVal > 0.0f ? (int16_t)((curVal / maxVal) * maxFillW) : 0;
        if (fillW < 0)
            fillW = 0;
        if (fillW > maxFillW)
            fillW = maxFillW;

        if (isEditing) {
            // 编辑态：4px 粗，提供更强的视觉存在感
            canvas->fillRoundRect(p.x + 3, p.y + 2, fillW, 4, 2, C_WHITE);
        } else {
            // 普通态：2px 细，保持优雅
            if (fillW > 2)
                canvas->fillRoundRect(p.x + 3, p.y + 3, fillW, 2, 1, C_WHITE);
            else if (fillW > 0)
                canvas->fillRect(p.x + 3, p.y + 3, fillW, 2, C_WHITE);
        }
    }
};

class Slider : public Widget {
   public:
    float* value;
    float maxVal;
    int16_t width;
    NanoUI::Callback<void(float)> onChanged;
    Slider(float& v, float m, int16_t w = 64, NanoUI::Callback<void(float)> cb = nullptr)
        : value(&v), maxVal(m), width(w), onChanged(cb) {}
    // Preserve the existing three-argument callback form used by consumers.
    Slider(float& v, float m, NanoUI::Callback<void(float)> cb) : Slider(v, m, 64, cb) {}
    RenderObject* createRenderObject() override {
        return new RenderSlider(value, maxVal, width, onChanged);
    }
};

// =========================================================
// NumberPicker: 经典对齐版
// =========================================================
class RenderNumberPicker : public RenderObject {
   public:
    int* value;
    int minVal, maxVal;
    const char* format;
    bool isEditing = false;
    SpringAnimator scrollAnim;

    RenderNumberPicker(int* v, int min, int max, const char* fmt)
        : value(v),
          minVal(min),
          maxVal(max),
          format(fmt ? fmt : "%d"),
          scrollAnim((float)*v, 0.2f, 0.8f) {
        size = {36, 18};
    }

    bool canFocus() override { return true; }

    bool handleEvent(Event e) override {
        if (!hasFocus)
            return false;
        if (e.type == EVT_OK) {
            isEditing = !isEditing;
            return true;
        }
        if (isEditing) {
            int old = *value;
            if (e.type == EVT_UP || e.type == EVT_LEFT)
                (*value)--;
            else if (e.type == EVT_DOWN || e.type == EVT_RIGHT)
                (*value)++;
            if (*value < minVal)
                *value = maxVal;
            if (*value > maxVal)
                *value = minVal;
            if (old != *value) {
                if (abs(*value - old) > 1)
                    scrollAnim.jumpTo((float)old + (*value > old ? -1 : 1));
                scrollAnim.set((float)*value);
                return true;
            }
        }
        return false;
    }

    void performLayout() override { size = constraints.constrainSize({36, 18}); }

    void paintWorker(Canvas* canvas, Point p) override {
        if (size.width <= 0 || size.height <= 0)
            return;
        float curScroll = scrollAnim.update();
        if (!scrollAnim.isStable() && WindowManager::instance)
            WindowManager::instance->markDirty();

        if (isEditing)
            canvas->fillRoundRect(p.x, p.y, 36, 18, 3, C_WHITE);
        else
            canvas->drawRoundRect(p.x, p.y, 36, 18, 3, C_WHITE);

        {
            ScopedClip clip(canvas,
                            {p.x, p.y, NanoCoordinate(static_cast<int32_t>(p.x) + size.width),
                             NanoCoordinate(static_cast<int32_t>(p.y) + size.height)});
            int16_t centerYA = p.y + 3;
            float offset = (curScroll - floor(curScroll));
            int centerVal = (int)floor(curScroll);

            auto drawDigit = [&](int val, int16_t yOffset) {
                char buf[8];
                snprintf(buf, sizeof(buf), format, val);
                canvas->drawText(p.x + 6, centerYA + yOffset, buf, 12, isEditing, 0);
            };

            drawDigit(centerVal, (int16_t)(-offset * 18));
            int nextVal = centerVal + 1;
            if (nextVal > maxVal)
                nextVal = minVal;
            drawDigit(nextVal, (int16_t)((1.0f - offset) * 18));
            int prevVal = centerVal - 1;
            if (prevVal < minVal)
                prevVal = maxVal;
            drawDigit(prevVal, (int16_t)((-1.0f - offset) * 18));
        }

        if (isEditing) {
            canvas->drawLine(p.x, p.y + 9, p.x + 3, p.y + 6, C_BLACK);
            canvas->drawLine(p.x, p.y + 9, p.x + 3, p.y + 12, C_BLACK);
            canvas->drawLine(p.x + 36, p.y + 9, p.x + 33, p.y + 6, C_BLACK);
            canvas->drawLine(p.x + 36, p.y + 9, p.x + 33, p.y + 12, C_BLACK);
        }
    }
};

class NumberPicker : public Widget {
   public:
    int* value;
    int minVal, maxVal;
    const char* format;
    NumberPicker(int* v, int min, int max, const char* fmt = "%d")
        : value(v), minVal(min), maxVal(max), format(fmt ? fmt : "%d") {}
    RenderObject* createRenderObject() override {
        return new RenderNumberPicker(value, minVal, maxVal, format);
    }
};

// =========================================================
// SlidingText: 阻尼弹簧物理版
// =========================================================
class RenderSlidingText : public RenderObject {
   public:
    const NanoUI::NanoUIText* dataSource;
    NanoUI::NanoUIText currentStr, oldStr;
    uint8_t fontSize;
    uint8_t fontId;
    Animator anim;

    RenderSlidingText(const NanoUI::NanoUIText* source, uint8_t size, uint8_t fid = 0)
        : dataSource(source), fontSize(size), fontId(fid), anim(0.0f, 0.3f) {
        if (dataSource) {
            currentStr = *dataSource;
            oldStr = *dataSource;
        }
    }

    void performLayout() override {
        syncSource();
        int16_t w = get_utf8_string_width(currentStr.c_str(), fontSize, fontId);
        size = constraints.constrainSize({w, (int16_t)fontSize});
    }

    void syncSource() {
        if (dataSource && *dataSource != currentStr) {
            oldStr = currentStr;
            currentStr = *dataSource;
            anim.jumpTo((float)fontSize);
            anim.set(0.0f);
        }
    }

    void paintWorker(Canvas* canvas, Point p) override {
        if (!dataSource)
            return;
        int16_t h = size.height;
        if (*dataSource != currentStr) {
            syncSource();
            markNeedsLayout();
        }
        float offsetF = anim.update();
        if (!anim.isStable() && WindowManager::instance)
            WindowManager::instance->markDirty();
        int16_t offsetI = (int16_t)offsetF;
        ScopedClip clip(canvas, {p.x, p.y, NanoCoordinate(static_cast<int32_t>(p.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(p.y) + size.height)});
        if (!anim.isStable())
            canvas->drawText(p.x, p.y - (h - offsetI), oldStr.c_str(), fontSize, false, fontId);
        canvas->drawText(p.x, p.y + offsetI, currentStr.c_str(), fontSize, false, fontId);
    }
};

class SlidingText : public Widget {
   public:
    const NanoUI::NanoUIText* source;
    uint8_t size;
    uint8_t fontId;
    SlidingText(const NanoUI::NanoUIText* s, uint8_t sz = 12, uint8_t fid = 0)
        : source(s), size(sz), fontId(fid) {}
    RenderObject* createRenderObject() override {
        return new RenderSlidingText(source, size, fontId);
    }
};

// Additional existing BW16 control, retained as a portable widget.
class RenderFlipClockText : public RenderObject {
   public:
    const NanoUI::NanoUIText* dataSource;
    NanoUI::NanoUIText currentStr, oldStr;
    uint8_t fontSize;
    uint8_t fontId;
    float progress = 1.0f;

    RenderFlipClockText(const NanoUI::NanoUIText* source, uint8_t size, uint8_t fid = 0)
        : dataSource(source), fontSize(size), fontId(fid) {
        if (dataSource) {
            currentStr = *dataSource;
            oldStr = *dataSource;
        }
    }

    void performLayout() override {
        syncSource();
        int16_t textW = get_utf8_string_width(currentStr.c_str(), fontSize, fontId);
        size = constraints.constrainSize({NanoExtent(static_cast<int32_t>(textW) + 4),
                                          NanoExtent(static_cast<int32_t>(fontSize) + 2)});
    }

    void syncSource() {
        if (dataSource && *dataSource != currentStr) {
            oldStr = currentStr;
            currentStr = *dataSource;
            progress = 0.0f;
        }
    }

    void paintWorker(Canvas* canvas, Point p) override {
        if (!dataSource)
            return;
        if (*dataSource != currentStr) {
            syncSource();
            markNeedsLayout();
        }

        if (progress < 1.0f) {
            progress += 0.20f;
            if (progress > 1.0f)
                progress = 1.0f;
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        }

        int16_t w = size.width;
        int16_t h = size.height;

        canvas->fillRect(p.x, p.y, w, h, C_BLACK);
        canvas->drawRoundRect(p.x, p.y, w, h, 2, C_WHITE);

        NanoUI::NanoUIText displayStr = (progress < 0.5f) ? oldStr : currentStr;
        canvas->drawText(p.x + 2, p.y + 1, displayStr.c_str(), fontSize, false, fontId);

        if (progress < 1.0f) {
            float shutterRatio = (progress < 0.5f) ? (progress * 2.0f) : ((1.0f - progress) * 2.0f);
            int16_t shutterH = (int16_t)((h / 2) * shutterRatio);
            canvas->fillRect(p.x + 1, p.y + 1, w - 2, shutterH, C_BLACK);
            canvas->fillRect(p.x + 1, p.y + h - 1 - shutterH, w - 2, shutterH, C_BLACK);
        }
        canvas->drawLine(p.x, p.y + h / 2, p.x + w, p.y + h / 2, C_BLACK);
    }
};

class FlipClockText : public Widget {
   public:
    const NanoUI::NanoUIText* source;
    uint8_t size;
    uint8_t fontId;
    FlipClockText(const NanoUI::NanoUIText* s, uint8_t sz = 12, uint8_t fid = 0)
        : source(s), size(sz), fontId(fid) {}
    RenderObject* createRenderObject() override {
        return new RenderFlipClockText(source, size, fontId);
    }
};

#endif
