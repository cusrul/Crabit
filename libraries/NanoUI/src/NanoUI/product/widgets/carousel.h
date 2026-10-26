#ifndef WIDGETS_CAROUSEL_H
#define WIDGETS_CAROUSEL_H

#include "../rendering/render_object.h"
#include "../core/window_manager.h"
#include "../core/animator.h"
#include "../core/embedded_vector.h"
#include <cmath>
#include "../core/text.h"

using NanoUITitle = NanoUI::NanoUIText;
using NanoUITitleList = NanoUI::EmbeddedVector<NanoUITitle>;

class RenderCarousel : public RenderObject {
   public:
    NanoUI::EmbeddedVector<RenderObject*> children;
    int selectedIndex = 0;
    int16_t itemSpacing;
    NanoUI::StateKey stateKey;

    SpringAnimator scrollAnim;
    bool isFirstFrame = true;

    RenderCarousel(int16_t spacing, NanoUI::StateKey key)
        : itemSpacing(spacing), stateKey(key), scrollAnim(0, 0.15f, 0.75f) {}

    ~RenderCarousel() override {
        for (auto c : children) {
            if (c)
                delete c;
        }
        children.clear();
    }

    void visitChildren(RenderObjectVisitor visitor) override {
        for (auto c : children)
            visitor(c);
    }

    bool canFocus() override { return !children.empty(); }

    bool handleEvent(Event e) override {
        if (children.empty())
            return false;
        selectedIndex = NanoMax(0, NanoMin(selectedIndex, static_cast<int>(children.size()) - 1));
        if (children[selectedIndex] && children[selectedIndex]->handleEvent(e))
            return true;

        int oldIndex = selectedIndex;
        if ((e.type == EVT_DOWN || e.type == EVT_RIGHT) &&
            selectedIndex < (int)children.size() - 1) {
            selectedIndex++;
        } else if ((e.type == EVT_UP || e.type == EVT_LEFT) && selectedIndex > 0) {
            selectedIndex--;
        }

        if (selectedIndex != oldIndex) {
            if (WindowManager::instance && NanoUI::stateKeyValid(stateKey)) {
                WindowManager::instance->saveState(stateKey, selectedIndex);
            }
            if (WindowManager::instance)
                WindowManager::instance->markDirty();
            return true;
        }
        return false;
    }

    void performLayout() override {
        if (WindowManager::instance && NanoUI::stateKeyValid(stateKey)) {
            selectedIndex = WindowManager::instance->getState(stateKey, 0);
        }
        if (children.empty())
            selectedIndex = 0;
        else
            selectedIndex =
                NanoMax(0, NanoMin(selectedIndex, static_cast<int>(children.size()) - 1));

        int32_t xOffset = 0;
        int32_t maxChildH = 0;
        for (auto child : children) {
            if (!child)
                continue;
            child->layout(BoxConstraints::loose(constraints.maxSize()));
            child->pos = {NanoCoordinate(xOffset), 0};
            xOffset += static_cast<int32_t>(child->size.width) + itemSpacing;
            if (child->size.height > maxChildH)
                maxChildH = child->size.height;
        }
        size = constraints.constrainSize(
            {constraints.maxWidth, NanoExtent(maxChildH + (children.empty() ? 0 : 8))});
        for (auto child : children) {
            if (child)
                child->pos.y =
                    NanoCoordinate((static_cast<int32_t>(size.height) - child->size.height) / 2);
        }
    }

    void reportToSystem(Point absPos) override {
        // RenderObject reports before paintWorker. This scrolling focus scope
        // reports below, after establishing this frame's transform and clip.
        (void)absPos;
    }

    void reportSelectedFocus(Point absPos) {
        if (children.empty() || selectedIndex < 0 ||
            selectedIndex >= static_cast<int>(children.size()) || !children[selectedIndex])
            return;
        RenderObject* sel = children[selectedIndex];
        float currentScrollX = scrollAnim.value;
        int16_t screenX = NanoCoordinate(static_cast<int32_t>(absPos.x) +
                                         static_cast<int32_t>(currentScrollX) + sel->pos.x);
        int16_t screenY = NanoCoordinate(static_cast<int32_t>(absPos.y) + sel->pos.y);
        if (WindowManager::instance) {
            WindowManager::instance->reportFocusRect(
                {screenX, screenY, NanoCoordinate(static_cast<int32_t>(screenX) + sel->size.width),
                 NanoCoordinate(static_cast<int32_t>(screenY) + sel->size.height)},
                sel->getRadius());  // 沿用子组件的圆角
        }
    }

    void paintWorker(Canvas* canvas, Point absPos) override {
        if (children.empty())
            return;
        selectedIndex = NanoMax(0, NanoMin(selectedIndex, static_cast<int>(children.size()) - 1));
        RenderObject* sel = children[selectedIndex];
        if (!sel)
            return;
        float targetScrollX = (size.width / 2.0f) - (sel->pos.x + sel->size.width / 2.0f);

        if (isFirstFrame) {
            scrollAnim.jumpTo(targetScrollX);
            isFirstFrame = false;
        } else {
            scrollAnim.set(targetScrollX);
        }

        float currentScrollX = scrollAnim.update();
        if (!scrollAnim.isStable() && WindowManager::instance)
            WindowManager::instance->markDirty();

        ScopedClip clip(canvas, {absPos.x, absPos.y,
                                 NanoCoordinate(static_cast<int32_t>(absPos.x) + size.width),
                                 NanoCoordinate(static_cast<int32_t>(absPos.y) + size.height)});
        if (hasFocus)
            reportSelectedFocus(absPos);
        for (int i = 0; i < (int)children.size(); i++) {
            RenderObject* child = children[i];
            if (!child)
                continue;
            child->hasFocus = (i == selectedIndex && this->hasFocus);
            Point drawOffset = {NanoCoordinate(static_cast<int32_t>(absPos.x) +
                                               static_cast<int32_t>(currentScrollX)),
                                absPos.y};
            int32_t childScreenX = static_cast<int32_t>(drawOffset.x) + child->pos.x;
            if (childScreenX + child->size.width > absPos.x &&
                childScreenX < absPos.x + size.width) {
                child->paint(canvas, drawOffset);
            }
        }
    }
};

class Carousel : public MultiChildWidget {
   public:
    Carousel(NanoUI::EmbeddedVector<Widget*> c, NanoUITitleList t = {}, int16_t s = 12,
             NanoUI::StateKey key = NanoUI::StateKey{})
        : MultiChildWidget(std::move(c)), spacing(s), stateKey(key), titles(std::move(t)) {}

    int16_t spacing;
    NanoUI::StateKey stateKey;
    NanoUITitleList titles;
    template <typename Container, typename TitleContainer>
    Carousel(const Container& c, const TitleContainer& t, int16_t s = 12,
             NanoUI::StateKey key = NanoUI::StateKey{})
        : MultiChildWidget(c), spacing(s), stateKey(key), titles(t.begin(), t.end()) {}

    RenderObject* createRenderObject() override {
        RenderCarousel* ro = new RenderCarousel(spacing, stateKey);
        for (auto w : children) {
            RenderObject* r = w->createRenderObject();
            r->parent = ro;
            ro->children.push_back(r);
        }
        return ro;
    }
};

#endif
