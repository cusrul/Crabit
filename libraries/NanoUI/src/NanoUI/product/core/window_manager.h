#ifndef CORE_WINDOW_MANAGER_H
#define CORE_WINDOW_MANAGER_H

#include "../framework/widget.h"
#include "../framework/app.h"
#include "../rendering/render_object.h"
#include "../hal/canvas.h"
#include "../hal/input.h"
#include "../core/animator.h"
#include "../core/fixed_state.h"
#include "../core/embedded_vector.h"
#include "../core/shutdown_guard.h"

struct PageEntry {
    Widget* widget;
    int savedFocusIndex;
    NanoUI::PageStateTable<8> states;
    Application* owner;
    Widget* (*rebuild)(Application*);
};

class WindowManager {
   public:
    static WindowManager* instance;
    VoidCallback onSystemBackPressed = nullptr;

   private:
    Canvas* canvas;
    InputDriver* input;
    RenderObject* rootRenderObject = nullptr;
    bool isRootDirty = true;
    bool isAnimating = false;
    bool clearing = false;
    bool layoutRequested_ = false;

    NanoUI::EmbeddedVector<RenderObject*> focusableWidgets;
    int focusIndex = 0;
    NanoUI::EmbeddedVector<PageEntry> pageStack;

    SpringAnimator xAnim, yAnim, wAnim, hAnim, rAnim;
    bool focusActive = false;
    Rect currentFocusTarget;
    Rect currentFocusClip{0, 0, 0, 0};
    int16_t currentFocusRadius = 0;
    bool hasFocusTargetThisFrame = false;

   public:
    WindowManager(Canvas* c, InputDriver* i)
        : canvas(c),
          input(i),
          xAnim(0, 0.15f, 0.6f),
          yAnim(0, 0.15f, 0.6f),
          wAnim(0, 0.15f, 0.6f),
          hAnim(0, 0.15f, 0.6f),
          rAnim(0, 0.15f, 0.6f) {
        instance = this;
    }

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    ~WindowManager() {
        clear();
        if (instance == this)
            instance = nullptr;
    }

    // Full teardown, not Back: do not rebuild an ancestor or destroy borrowed
    // Applications. Canvas, InputDriver and the allocator must still be alive.
    void clear() {
        if (clearing)
            return;
        NanoUI::detail::ShutdownGuard guard;
        clearing = true;
        onSystemBackPressed = nullptr;
        focusableWidgets = decltype(focusableWidgets){};
        RenderObject* root = rootRenderObject;
        rootRenderObject = nullptr;
        delete root;  // Render nodes can borrow data from retained Widgets/Apps.
        while (!pageStack.empty()) {
            PageEntry entry = pageStack.back();
            pageStack.pop_back();
            releasePageWidget(entry);
        }
        pageStack = decltype(pageStack){};
        focusIndex = 0;
        focusActive = hasFocusTargetThisFrame = false;
        currentFocusTarget = currentFocusClip = Rect{0, 0, 0, 0};
        currentFocusRadius = 0;
        xAnim.jumpTo(0);
        yAnim.jumpTo(0);
        wAnim.jumpTo(0);
        hAnim.jumpTo(0);
        rAnim.jumpTo(0);
        isRootDirty = true;
        isAnimating = false;
        layoutRequested_ = false;
        clearing = false;
    }

    void reportFocusRect(Rect r, int16_t radius) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        const Rect clip =
            canvas->getClipRect().intersection({0, 0, canvas->width(), canvas->height()});
        if (!r.intersects(clip))
            return;
        currentFocusClip = clip;
        currentFocusTarget = r;
        currentFocusRadius = radius;
        hasFocusTargetThisFrame = true;
    }

    void saveState(NanoUI::StateKey key, int val) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (pageStack.empty())
            return;
        pageStack.back().states.set(key, val);
    }

    int getState(NanoUI::StateKey key, int def = 0) {
        if (pageStack.empty())
            return def;
        return pageStack.back().states.get(key, def);
    }

    RenderObject* getFocusedRenderObject() {
        if (focusIndex >= 0 && focusIndex < (int)focusableWidgets.size())
            return focusableWidgets[focusIndex];
        return nullptr;
    }

    size_t getStackSize() const { return pageStack.size(); }

    void push(Widget* page, Application* owner = nullptr,
              Widget* (*rebuild)(Application*) = nullptr) {
        if (NanoUI::detail::shutdownInProgress()) {
            delete page;
            return;
        }
        if (!pageStack.empty())
            pageStack.back().savedFocusIndex = this->focusIndex;
        pageStack.push_back({page, 0, {}, owner, rebuild});
        focusIndex = 0;
        loadPage(page);
        if (owner)
            releasePageWidget(pageStack.back());
    }
    void pop() {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (pageStack.size() <= 1)
            return;
        PageEntry current = pageStack.back();
        pageStack.pop_back();
        releasePageWidget(current);
        this->focusIndex = pageStack.back().savedFocusIndex;
        PageEntry& previous = pageStack.back();
        if (!previous.widget && previous.owner) {
            previous.widget =
                previous.rebuild ? previous.rebuild(previous.owner) : previous.owner->buildUI();
        }
        loadPage(previous.widget);
        if (previous.owner)
            releasePageWidget(previous);
    }
    void loadPage(Widget* page) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (!page)
            return;
        focusableWidgets.clear();  // Old nodes leave with the previous render tree.
        if (rootRenderObject)
            delete rootRenderObject;
        rootRenderObject = page->createRenderObject();
        rootRenderObject->markNeedsLayout();
        performLayoutAndFocus();
        isRootDirty = true;
    }

    static void releasePageWidget(PageEntry& entry) {
        Widget* widget = entry.widget;
        entry.widget = nullptr;
        delete widget;
    }

    void update() {
        if (NanoUI::detail::shutdownInProgress() || !input)
            return;
        UIEvent e = input->poll();
        if (e.type != EVT_NONE) {
            Event re;
            re.type = e.type;
            dispatchEvent(re);
        }
    }

    void drawFrame() {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (!rootRenderObject)
            return;
        performLayoutAndFocus();
        hasFocusTargetThisFrame = false;
        bool focusAnimating = !xAnim.isStable() || !yAnim.isStable() || !wAnim.isStable() ||
                              !hAnim.isStable() || !rAnim.isStable();

        if (isRootDirty || isAnimating || focusAnimating) {
            isRootDirty = false;
            isAnimating = false;
            canvas->clear();
            canvas->resetClipRect();
            rootRenderObject->paint(canvas, {0, 0});

            if (hasFocusTargetThisFrame) {
                focusActive = true;
                if (wAnim.value == 0) {
                    xAnim.jumpTo((float)currentFocusTarget.left);
                    yAnim.jumpTo((float)currentFocusTarget.top);
                    wAnim.jumpTo((float)(currentFocusTarget.right - currentFocusTarget.left));
                    hAnim.jumpTo((float)(currentFocusTarget.bottom - currentFocusTarget.top));
                    rAnim.jumpTo((float)currentFocusRadius);
                } else {
                    if (std::abs(xAnim.target - (float)currentFocusTarget.left) > 5.0f)
                        xAnim.velocity = 0;
                    if (std::abs(yAnim.target - (float)currentFocusTarget.top) > 5.0f)
                        yAnim.velocity = 0;
                    xAnim.set((float)currentFocusTarget.left);
                    yAnim.set((float)currentFocusTarget.top);
                    wAnim.set((float)(currentFocusTarget.right - currentFocusTarget.left));
                    hAnim.set((float)(currentFocusTarget.bottom - currentFocusTarget.top));
                    rAnim.set((float)currentFocusRadius);
                }
            } else
                focusActive = false;

            xAnim.update();
            yAnim.update();
            wAnim.update();
            hAnim.update();
            rAnim.update();
            if (focusActive && wAnim.value > 1) {
                ScopedClip clip(canvas, currentFocusClip);
                // 聚焦圆角在目标圆角基础上 +2 像素。
                canvas->fillRoundRect((int16_t)xAnim.value - 2, (int16_t)yAnim.value - 2,
                                      (int16_t)wAnim.value + 4, (int16_t)hAnim.value + 4,
                                      (int16_t)rAnim.value + 2, C_INVERT);
            }
            canvas->display();
        }
    }

    void performLayoutAndFocus() {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (rootRenderObject && rootRenderObject->needsLayout) {
            // Cached nodes may already have been replaced by an external child
            // update. Discard addresses without dereferencing them.
            focusableWidgets.clear();
            layoutRequested_ = false;
            rootRenderObject->layout(BoxConstraints::tight(canvas->width(), canvas->height()));
            // RenderObject::layout clears its own flag on return. Preserve a
            // popup update requested by a factory/layout for the next frame.
            if (layoutRequested_)
                rootRenderObject->needsLayout = true;
            rebuildFocusList();
            isRootDirty = true;
        }
    }

    void markDirty() {
        isRootDirty = true;
        isAnimating = true;
    }
    void markNeedsLayout() {
        isRootDirty = true;
        layoutRequested_ = true;
        if (rootRenderObject)
            rootRenderObject->needsLayout = true;
    }

    void dispatchEvent(Event e) {
        if (NanoUI::detail::shutdownInProgress())
            return;
        if (e.type == EVT_BACK) {
            if (onSystemBackPressed)
                onSystemBackPressed();
            else
                this->pop();
            return;
        }
        // A popup may have been replaced since the last draw. Refresh render
        // borrowers and focus before delivering OK to the previously focused node.
        performLayoutAndFocus();
        if (!rootRenderObject || focusableWidgets.empty())
            return;
        if (focusIndex >= 0 && focusIndex < (int)focusableWidgets.size()) {
            if (focusableWidgets[focusIndex]->handleEvent(e)) {
                markDirty();
                return;
            }
        }
        bool changed = false;
        if (e.type == EVT_DOWN || e.type == EVT_RIGHT) {
            focusIndex = (focusIndex + 1) % focusableWidgets.size();
            changed = true;
        } else if (e.type == EVT_UP || e.type == EVT_LEFT) {
            focusIndex = (focusIndex - 1 + focusableWidgets.size()) % focusableWidgets.size();
            changed = true;
        }
        if (changed) {
            rebuildFocusList();
            markDirty();
        }
    }

    void rebuildFocusList() {
        if (NanoUI::detail::shutdownInProgress())
            return;
        focusableWidgets.clear();
        recursiveScanFocus(rootRenderObject);
        if (focusableWidgets.empty()) {
            focusIndex = -1;
            return;
        }
        if (focusIndex >= (int)focusableWidgets.size() || focusIndex < 0)
            focusIndex = 0;
        for (int i = 0; i < (int)focusableWidgets.size(); i++)
            focusableWidgets[i]->hasFocus = (i == focusIndex);
    }
    void recursiveScanFocus(RenderObject* node) {
        if (!node)
            return;
        if (node->canFocus())
            focusableWidgets.push_back(node);
        if (node->canFocus())
            return;
        node->visitChildren(
            [](RenderObject* c) { WindowManager::instance->recursiveScanFocus(c); });
    }
};

class Navigator {
   public:
    static void init(WindowManager* wm) { (void)wm; }
    static void push(Widget* page) {
        if (WindowManager::instance)
            WindowManager::instance->push(page);
    }
    static void pop() {
        if (WindowManager::instance)
            WindowManager::instance->pop();
    }
};

inline void RenderObject::reportToSystem(Point absPos) {
    if (WindowManager::instance) {
        WindowManager::instance->reportFocusRect(
            {absPos.x, absPos.y, (int16_t)(absPos.x + size.width),
             (int16_t)(absPos.y + size.height)},
            getRadius()  // 获取当前圆角
        );
    }
}
#endif
