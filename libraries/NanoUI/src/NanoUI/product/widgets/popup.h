#ifndef WIDGETS_POPUP_H
#define WIDGETS_POPUP_H

#include "../framework/widget.h"
#include "../rendering/render_object.h"
#include "../core/window_manager.h"
#include <cstring>  // for strlen
#include <cstdio>
#include "text.h"
#include "container.h"
#include "button.h"
#include "flex.h"

// =========================================================
// Popup: 静态控制类
// =========================================================
class PopupRoot;

class Popup {
   public:
    static Widget* pendingContent;
    static bool isVisible;
    static int16_t targetW;
    static int16_t targetH;

    // --- Toast 状态 ---
    static const char* toastMsg;
    static uint32_t toastEndTime;

    // Borrowed configuration, including during shutdown rejection. The caller owns
    // it and all referenced state until its popup RenderObjects have been removed.
    static void show(Widget* content, int16_t w = 0, int16_t h = 0) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        pendingContent = content;
        targetW = w;
        targetH = h;
        isVisible = content != nullptr;
        ++revision_;
        if (WindowManager::instance)
            WindowManager::instance->markNeedsLayout();  // 触发重排
    }

    static void hide() {
        if (NanoUI::detail::shutdownInProgress())
            return;
        isVisible = false;
        pendingContent = nullptr;
        ++revision_;
        if (WindowManager::instance)
            WindowManager::instance->markNeedsLayout();  // 触发重排
    }

    // [新增] 显示 Toast 消息 (默认显示 1.5秒)
    static void showToast(const char* msg, int duration = 1500) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (!msg || !*msg || duration <= 0) {
            clearToast();
            return;
        }
        const size_t bytes = std::strlen(msg) + 1;
        char* copy = static_cast<char*>(NanoUI::MemoryPool::instance().allocate(bytes, "UI_Text"));
        if (!copy)
            return;  // Preserve the previous Toast if allocation fails.
        std::memcpy(copy, msg, bytes);
        clearToast();  // Copy first: msg may refer to the previous Toast itself.
        toastStorage_ = copy;
        toastMsg = copy;
        toastEndTime = hal_get_ticks() + duration;
        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }

    // Terminal cleanup only, after all popup RenderObjects have been removed.
    static void reset() {
        NanoUI::detail::ShutdownGuard guard;
        pendingContent = nullptr;
        ++revision_;
        isVisible = false;
        targetW = targetH = 0;
        clearToast();
    }

   private:
    friend class RenderPopupRoot;
    static uint32_t revision_;
    static char* toastStorage_;

    static void clearToast() {
        char* storage = toastStorage_;
        toastStorage_ = nullptr;
        toastMsg = nullptr;
        toastEndTime = 0;
        if (storage)
            NanoUI::MemoryPool::instance().deallocate(storage, std::strlen(storage) + 1, "UI_Text");
        if (WindowManager::instance)
            WindowManager::instance->markDirty();
    }
};

// =========================================================
// RenderPopupRoot: 渲染层
// =========================================================
class RenderPopupRoot : public RenderObject {
   public:
    RenderObject* child = nullptr;
    RenderObject* popup = nullptr;
    Size popupFrame = {0, 0};

   private:
    uint32_t popupRevision_ = 0;
    static void clearLiveFocus(RenderObject* node) {
        if (!node)
            return;
        node->hasFocus = false;
        node->visitChildren([](RenderObject* child) { clearLiveFocus(child); });
    }

   public:
    // 销毁根节点时同时销毁其弹窗。
    ~RenderPopupRoot() override {
        if (child)
            delete child;
        if (popup)
            delete popup;
    }
    void drawRoundedBox(Canvas* canvas, int16_t x, int16_t y, int16_t w, int16_t h) {
        if (w <= 0 || h <= 0)
            return;
        canvas->fillRect(x + 3, y, w - 6, h, C_BLACK);
        canvas->fillRect(x, y + 3, w, h - 6, C_BLACK);
        canvas->fillRect(x + 1, y + 1, w - 2, h - 2, C_BLACK);
        canvas->drawLine(x + 3, y, x + w - 3, y, C_WHITE);
        canvas->drawLine(x + 3, y + h - 1, x + w - 3, y + h - 1, C_WHITE);
        canvas->drawLine(x, y + 3, x, y + h - 3, C_WHITE);
        canvas->drawLine(x + w - 1, y + 3, x + w - 1, y + h - 3, C_WHITE);
        canvas->drawPixelWithClip(x + 1, y + 1, C_WHITE);
        canvas->drawPixelWithClip(x + 2, y, C_WHITE);
        canvas->drawPixelWithClip(x, y + 2, C_WHITE);
        canvas->drawPixelWithClip(x + w - 2, y + 1, C_WHITE);
        canvas->drawPixelWithClip(x + w - 3, y, C_WHITE);
        canvas->drawPixelWithClip(x + w - 1, y + 2, C_WHITE);
        canvas->drawPixelWithClip(x + 1, y + h - 2, C_WHITE);
        canvas->drawPixelWithClip(x + 2, y + h - 1, C_WHITE);
        canvas->drawPixelWithClip(x, y + h - 3, C_WHITE);
        canvas->drawPixelWithClip(x + w - 2, y + h - 2, C_WHITE);
        canvas->drawPixelWithClip(x + w - 3, y + h - 1, C_WHITE);
        canvas->drawPixelWithClip(x + w - 1, y + h - 3, C_WHITE);
    }

    void visitChildren(RenderObjectVisitor visitor) override {
        if (Popup::isVisible) {
            if (popup && popupRevision_ == Popup::revision_)
                visitor(popup);
        } else if (child)
            visitor(child);
    }

    bool handleEvent(Event e) override {
        if (Popup::isVisible)
            return popup && popupRevision_ == Popup::revision_ ? popup->handleEvent(e) : false;
        return child ? child->handleEvent(e) : false;
    }

    void performLayout() override {
        if (child)
            child->layout(constraints);
        // Only traverse the live background subtree; the manager's old focus
        // cache may contain nodes already deleted by a child replacement.
        if (Popup::isVisible)
            clearLiveFocus(child);

        if (popup && (!Popup::isVisible || popupRevision_ != Popup::revision_)) {
            RenderObject* previous = popup;
            popup = nullptr;
            delete previous;
        }
        if (Popup::isVisible && Popup::pendingContent && !popup) {
            popupRevision_ = Popup::revision_;
            popup = Popup::pendingContent->createRenderObject();
            if (popup)
                popup->parent = this;
        }

        if (popup) {
            int16_t maxSafeW = NanoExtent(static_cast<int32_t>(constraints.maxWidth) - 20);
            int16_t maxSafeH = NanoExtent(static_cast<int32_t>(constraints.maxHeight) - 10);
            BoxConstraints boxC;

            if (Popup::targetW > 0) {
                boxC.minWidth = NanoMin(Popup::targetW, maxSafeW);
                boxC.maxWidth = boxC.minWidth;
            } else {
                boxC.minWidth = 0;
                boxC.maxWidth = maxSafeW;
            }

            if (Popup::targetH > 0) {
                boxC.minHeight = NanoMin(Popup::targetH, maxSafeH);
                boxC.maxHeight = boxC.minHeight;
            } else {
                boxC.minHeight = 0;
                boxC.maxHeight = maxSafeH;
            }

            popup->layout(boxC);
            popupFrame = constraints.loosen().constrainSize(
                {NanoExtent(static_cast<int32_t>(popup->size.width) + 6),
                 NanoExtent(static_cast<int32_t>(popup->size.height) + 6)});
        } else {
            popupFrame = {0, 0};
        }
        size = constraints.constrainSize(constraints.maxSize());
    }

    void paintWorker(Canvas* canvas, Point p) override {
        ScopedClip clip(canvas, {p.x, p.y, NanoCoordinate(static_cast<int32_t>(p.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(p.y) + size.height)});
        // 1. 画底层
        if (child)
            child->paint(canvas, p);

        // 2. 画模态弹窗
        if (Popup::isVisible && popup && popupRevision_ == Popup::revision_) {
            for (int32_t x = 0; x < size.width; x += 2) {
                for (int32_t y = 0; y < size.height; y += 2)
                    canvas->drawPixelWithClip(NanoCoordinate(p.x + x), NanoCoordinate(p.y + y),
                                              C_BLACK);
            }
            int16_t boxW = popupFrame.width;
            int16_t boxH = popupFrame.height;
            int16_t cx = NanoCoordinate(static_cast<int32_t>(p.x) + (size.width - boxW) / 2);
            int16_t cy = NanoCoordinate(static_cast<int32_t>(p.y) + (size.height - boxH) / 2);
            drawRoundedBox(canvas, cx, cy, boxW, boxH);
            popup->paint(canvas, {NanoCoordinate(static_cast<int32_t>(cx) + 3),
                                  NanoCoordinate(static_cast<int32_t>(cy) + 3)});
        }

        // 3. [新增] 画 Toast 消息 (位于最顶层)
        if (Popup::toastMsg && static_cast<int32_t>(Popup::toastEndTime - hal_get_ticks()) > 0) {
            int16_t textLen = get_utf8_string_width(Popup::toastMsg, 8, 0);
            int16_t tW = NanoExtent(static_cast<int32_t>(textLen) + 10);
            int16_t tH = 15;

            // 位置：底部居中
            int16_t tx = NanoCoordinate(static_cast<int32_t>(p.x) + (size.width - tW) / 2);
            int16_t ty = NanoCoordinate(static_cast<int32_t>(p.y) + size.height - 20);

            // 画黑底白框小气泡
            canvas->fillRect(tx, ty, tW, tH, C_BLACK);
            canvas->drawRect(tx, ty, tW, tH, C_WHITE);
            // 简单的阴影效果(可选)
            canvas->drawLine(tx + 1, ty + tH, tx + tW, ty + tH, C_BLACK);

            canvas->drawText(tx + 5, ty + 4, Popup::toastMsg, 8, false, 0);

            // 持续刷新以检查超时
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
        } else {
            // 超时后清空指针，停止绘制
            if (Popup::toastMsg) {
                Popup::clearToast();
            }
        }
    }
};

class PopupRoot : public SingleChildWidget {
   public:
    PopupRoot(Widget* app) : SingleChildWidget(app) {}
    RenderObject* createRenderObject() override {
        RenderPopupRoot* ro = new RenderPopupRoot();
        if (child) {
            ro->child = child->createRenderObject();
            ro->child->parent = ro;
        }
        return ro;
    }
};

#endif
