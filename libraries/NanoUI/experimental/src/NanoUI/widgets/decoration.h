#pragma once
#include "layout.h"

namespace NanoUI {
struct BoxStyle {
    BoxStyle(std::uint32_t fill = 0, std::uint32_t edge = 0, float width = 0) noexcept
        : background(fill), border(edge), borderWidth(width) {}
    std::uint32_t background, border;
    float borderWidth;
};
class RenderDecoration final : public RenderObject {
public:
    void configure(BoxStyle value) noexcept { style_ = value; markNeedsPaint(); }
protected:
    void performPaint(PaintCanvas& canvas) noexcept override {
        const auto s = size();
        canvas.fillRect(Rect::fromSize(s), style_.background);
        const float t = style_.borderWidth;
        if (t <= 0) return;
        canvas.fillRect(Rect(0, 0, s.width, t), style_.border);
        canvas.fillRect(Rect(0, s.height - t, s.width, s.height), style_.border);
        canvas.fillRect(Rect(0, 0, t, s.height), style_.border);
        canvas.fillRect(Rect(s.width - t, 0, s.width, s.height), style_.border);
    }
private:
    BoxStyle style_;
};
class DecoratedBox final : public RenderWidget {
public:
    DecoratedBox(BoxStyle style, ChildBuilder child, Key key = {}) noexcept
        : RenderWidget(typeId<DecoratedBox>(), key), style_(style), child_(child) {}
    BuildResult build(BuildContext& c) const noexcept override { return child_.build(c); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderDecoration>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        if (!std::isfinite(style_.borderWidth) || style_.borderWidth < 0) return UiError::invalidInput;
        static_cast<RenderDecoration&>(r).configure(style_); return UiError::none;
    }
private:
    BoxStyle style_;
    ChildBuilder child_;
};
} // namespace NanoUI
