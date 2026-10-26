#pragma once
#include "layout.h"

namespace NanoUI {
#if defined(__cpp_noexcept_function_type)
using ActionCallback = bool (*)(InputAction, void*) noexcept;
using FocusCallback = void (*)(bool, void*) noexcept;
#else
using ActionCallback = bool (*)(InputAction, void*);
using FocusCallback = void (*)(bool, void*);
#endif
class RenderInputRegion final : public RenderObject {
public:
    void configure(ActionCallback callback, void* payload, bool enabled = true,
                   FocusCallback focus = nullptr) noexcept {
        callback_ = callback; payload_ = payload; enabled_ = enabled; focus_ = focus;
    }
    bool canFocus() const noexcept override { return enabled_ && callback_; }
    void focusChanged(bool focused) noexcept override { if (focus_) focus_(focused, payload_); }
    bool handleInput(InputAction action) noexcept override {
        return enabled_ && callback_ && callback_(action, payload_);
    }
protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        Size extent;
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            child->layout(c); child->setOffset({});
            if (child->size().width > extent.width) extent.width = child->size().width;
            if (child->size().height > extent.height) extent.height = child->size().height;
        }
        return extent; // Interaction wrapper must not drop tight layout constraints.
    }
private:
    ActionCallback callback_ = nullptr;
    void* payload_ = nullptr;
    bool enabled_ = true;
    FocusCallback focus_ = nullptr;
};
class InputRegion final : public RenderWidget {
public:
    InputRegion(ActionCallback callback, void* payload, ChildBuilder child, Key key = {},
                bool enabled = true, FocusCallback focus = nullptr) noexcept
        : RenderWidget(typeId<InputRegion>(), key), callback_(callback), payload_(payload),
          child_(child), enabled_(enabled), focus_(focus) {}
    BuildResult build(BuildContext& c) const noexcept override { return child_.build(c); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderInputRegion>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        static_cast<RenderInputRegion&>(r).configure(callback_, payload_, enabled_, focus_); return UiError::none;
    }
private:
    ActionCallback callback_;
    void* payload_;
    ChildBuilder child_;
    bool enabled_;
    FocusCallback focus_;
};
} // namespace NanoUI
