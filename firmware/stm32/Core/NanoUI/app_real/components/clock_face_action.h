#pragma once

#include <NanoUI.h>

// Time-only faces keep the normal OK-to-menu action, without drawing a focus
// rectangle across the entire screen. Other buttons retain normal focus styling.
class RenderClockFaceAction : public RenderButton {
   public:
    explicit RenderClockFaceAction(VoidCallback action) : RenderButton(action) {}
    void reportToSystem(Point) override {}
};

class ClockFaceAction : public SingleChildWidget {
    VoidCallback action;

   public:
    ClockFaceAction(Widget* content, VoidCallback callback)
        : SingleChildWidget(content), action(callback) {}
    RenderObject* createRenderObject() override {
        auto* object = new RenderClockFaceAction(action);
        object->child = child->createRenderObject();
        object->child->parent = object;
        return object;
    }
};
