#ifndef WIDGETS_BUTTON_H
#define WIDGETS_BUTTON_H
#include "layout.h"
#include "../core/callback.h"

class RenderButton : public RenderSingleChildBox {
   public:
    NanoUI::Callback<void()> onPressed;
    uint8_t flashCounter = 0;

    RenderButton(NanoUI::Callback<void()> cb) : onPressed(cb) {}
    bool canFocus() override { return true; }

    bool handleEvent(Event e) override {
        if (hasFocus && e.type == EVT_OK) {
            flashCounter = 3;
            const auto callback = onPressed;
            if (callback)
                callback();
            return true;
        }
        return false;
    }

    void performLayout() override {
        if (child) {
            child->layout(constraints);
            child->pos = {0, 0};
            size = constraints.constrainSize(child->size);
        } else {
            size = constraints.constrainSize({0, 0});
        }
    }
};

class Button : public SingleChildWidget {
   public:
    NanoUI::Callback<void()> onPressed;
    Button(Widget* child, NanoUI::Callback<void()> cb) : SingleChildWidget(child), onPressed(cb) {}

    RenderObject* createRenderObject() override {
        RenderButton* ro = new RenderButton(onPressed);
        if (child) {
            ro->child = child->createRenderObject();
            ro->child->parent = ro;
        }
        return ro;
    }
};

#endif
