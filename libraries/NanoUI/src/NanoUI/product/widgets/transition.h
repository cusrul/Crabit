#ifndef WIDGETS_TRANSITION_H
#define WIDGETS_TRANSITION_H
#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/animator.h"
#include "../core/window_manager.h"
#include <cstdlib>
#include <stdint.h>

enum class TransitionType {
    SlideFromRight,  // 经典弹簧滑入
    CRT_Expand,      // CRT 开机效果：带扫描线上下展开
    CyberScan,       // 从左到右扫过逐渐显现
    FadeOutIn        // 先暗下再点亮
};

class RenderPageTransition : public RenderObject {
   public:
    RenderObject* child = nullptr;
    TransitionType type;
    SpringAnimator anim;
    bool animationInitialized = false;

    RenderPageTransition(TransitionType t) : type(t), anim(0, 0.15f, 0.7f) {
        switch (type) {
            case TransitionType::SlideFromRight:
                anim = SpringAnimator(1.0f, 0.02f, 0.9f);
                break;
            case TransitionType::CRT_Expand:
                anim = SpringAnimator(0.0f, 0.02f, 0.8f);
                break;
            case TransitionType::CyberScan:
                anim = SpringAnimator(0.0f, 0.02f, 0.9f);
                break;
            case TransitionType::FadeOutIn:
                anim = SpringAnimator(0.0f, 0.012f, 0.8f);
                break;
        }
    }

    ~RenderPageTransition() override {
        if (child)
            delete child;
    }
    void visitChildren(RenderObjectVisitor visitor) override {
        if (child)
            visitor(child);
    }
    bool canFocus() override { return child ? child->canFocus() : false; }
    bool handleEvent(Event e) override { return child ? child->handleEvent(e) : false; }

    void performLayout() override {
        if (child) {
            child->layout(constraints);
            child->pos = {0, 0};
            size = constraints.constrainSize(child->size);
        } else {
            size = constraints.constrainSize({0, 0});
        }
        // This animator uses normalized progress, unlike pixel-valued springs.
        // Keep its own tolerance below half a pixel; do not change global defaults.
        anim.threshold = 0.5f / NanoMax<int16_t>(1, NanoMax(size.width, size.height));
        if (!animationInitialized) {
            const float start = type == TransitionType::SlideFromRight ? 1.0f : 0.0f;
            const float finish = type == TransitionType::SlideFromRight ? 0.0f : 1.0f;
            anim.jumpTo(start);
            anim.set(finish);
            animationInitialized = true;
        }
    }

    void paintWorker(Canvas* canvas, Point absPos) override {
        if (!child)
            return;

        ScopedClip clip(canvas, {absPos.x, absPos.y,
                                 NanoCoordinate(static_cast<int32_t>(absPos.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(absPos.y) + size.height)});

        float progress = anim.update();
        if (anim.isStable()) {
            // The final scheduled frame must land exactly at the endpoint.
            // Paint the unmasked child: scan trails must not survive completion.
            anim.jumpTo(anim.target);
            child->paint(canvas, absPos);
            return;
        }
        if (WindowManager::instance)
            WindowManager::instance->markDirty();

        if (type == TransitionType::SlideFromRight) {
            Point offsetPos = {NanoCoordinate(static_cast<int32_t>(absPos.x) +
                                              static_cast<int32_t>(progress * size.width)),
                               absPos.y};
            if (offsetPos.x < absPos.x + size.width)
                child->paint(canvas, offsetPos);
        } else if (type == TransitionType::CRT_Expand) {
            child->paint(canvas, absPos);
            int16_t currentH = static_cast<int16_t>(progress * size.height);
            if (currentH < size.height) {
                int16_t topY = absPos.y + size.height / 2 - currentH / 2;
                int16_t botY = absPos.y + size.height / 2 + currentH / 2;
                if (topY > absPos.y)
                    canvas->fillRect(absPos.x, absPos.y, size.width, topY - absPos.y, C_BLACK);
                if (botY < absPos.y + size.height)
                    canvas->fillRect(absPos.x, botY, size.width, (absPos.y + size.height) - botY,
                                     C_BLACK);
                if (topY >= absPos.y && topY < absPos.y + size.height)
                    canvas->drawLine(absPos.x, topY, absPos.x + size.width, topY, C_WHITE);
                if (botY >= absPos.y && botY < absPos.y + size.height)
                    canvas->drawLine(absPos.x, botY, absPos.x + size.width, botY, C_WHITE);
            }
        } else if (type == TransitionType::CyberScan) {
            child->paint(canvas, absPos);
            int16_t scanX = static_cast<int16_t>(progress * size.width);
            if (scanX < size.width + 10) {
                if (scanX < size.width)
                    canvas->fillRect(absPos.x + scanX, absPos.y, size.width - scanX, size.height,
                                     C_BLACK);
                if (scanX >= 0 && scanX < size.width)
                    canvas->drawLine(absPos.x + scanX, absPos.y, absPos.x + scanX,
                                     absPos.y + size.height, C_WHITE);
                for (int w = 1; w < 10; w++) {
                    int16_t trailX = absPos.x + scanX - w;
                    if (trailX >= absPos.x && trailX < absPos.x + size.width) {
                        for (int y = 0; y < size.height; y += 2) {
                            if (rand() % 12 > w)
                                canvas->drawPixelWithClip(trailX, absPos.y + y, C_BLACK);
                        }
                    }
                }
            }
        } else if (type == TransitionType::FadeOutIn) {
            child->paint(canvas, absPos);
            // 0 -> 50 (黑显), 50 -> 100 (淡出黑)
            float effect = (progress < 0.5f) ? (0.5f - progress) : (progress - 0.5f);
            int threshold = static_cast<int>(effect * 10.0f);  // 0-5

            if (threshold < 5) {
                for (int x = 0; x < size.width; x += 2) {
                    for (int y = 0; y < size.height; y += 2) {
                        // 棋盘格抖动算法
                        if ((x + y) % 10 > threshold * 2) {
                            canvas->drawPixelWithClip(absPos.x + x, absPos.y + y, C_BLACK);
                            canvas->drawPixelWithClip(absPos.x + x + 1, absPos.y + y + 1, C_BLACK);
                        }
                    }
                }
            } else if (progress > 0.45f && progress < 0.55f) {
                canvas->fillRect(absPos.x, absPos.y, size.width, size.height, C_BLACK);
            }
        }
    }
};

class PageTransition : public SingleChildWidget {
   public:
    TransitionType type;
    PageTransition(Widget* c, TransitionType t = TransitionType::SlideFromRight)
        : SingleChildWidget(c), type(t) {}

    RenderObject* createRenderObject() override {
        RenderPageTransition* ro = new RenderPageTransition(type);
        if (child) {
            ro->child = child->createRenderObject();
            ro->child->parent = ro;
        }
        return ro;
    }
};

#endif
