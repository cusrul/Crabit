#pragma once

#include <NanoUI/framework/build_context.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/rendering/render_object.h>

namespace NanoUI {
// A builder recreates owned configurations. Its payload must outlive the Widget.
// Never retain a stack-local payload or return a success after factory OOM.
#if defined(__cpp_noexcept_function_type)
using ChildBuild = BuildResult (*)(BuildContext&, void*) noexcept;
#else
using ChildBuild = BuildResult (*)(BuildContext&, void*);
#endif
struct ChildBuilder {
    ChildBuilder(ChildBuild function = nullptr, void* data = nullptr) noexcept
        : callback(function), payload(data) {}
    BuildResult build(BuildContext& context) const noexcept {
        return callback ? callback(context, payload) : BuildResult{};
    }
    ChildBuild callback;
    void* payload;
};

enum class Axis { horizontal, vertical };
enum class CrossAlign { start, center, end, stretch };
enum class MainAlign { start, center, end, spaceBetween };
struct Insets {
    Insets(float all = 0) noexcept : left(all), top(all), right(all), bottom(all) {}
    Insets(float l, float t, float r, float b) noexcept : left(l), top(t), right(r), bottom(b) {}
    float left, top, right, bottom;
    bool valid() const noexcept {
        return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) &&
            std::isfinite(bottom) && left >= 0 && top >= 0 && right >= 0 && bottom >= 0;
    }
};

class RenderLinear final : public RenderObject {
public:
    void configure(Axis axis, float gap, CrossAlign align, MainAlign main = MainAlign::start) noexcept {
        axis_ = axis; gap_ = gap; align_ = align; main_ = main; markNeedsLayout();
    }
protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        const bool vertical = axis_ == Axis::vertical;
        const float crossMax = vertical ? c.maxWidth : c.maxHeight;
        const float crossMin = align_ == CrossAlign::stretch && std::isfinite(crossMax) ? crossMax : 0;
        const float infinity = std::numeric_limits<float>::infinity();
        const BoxConstraints childConstraints = vertical
            ? BoxConstraints(crossMin, crossMax, 0, infinity)
            : BoxConstraints(0, infinity, crossMin, crossMax);
        float main = 0, cross = 0;
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            child->layout(childConstraints);
            const auto s = child->size();
            main += vertical ? s.height : s.width;
            const float candidate = vertical ? s.width : s.height;
            if (candidate > cross) cross = candidate;
            if (child->nextSibling()) main += gap_;
        }
        const float available = vertical ? c.maxHeight : c.maxWidth;
        const float requested = main_ != MainAlign::start && std::isfinite(available) ? available : main;
        const auto result = c.constrain(vertical ? Size(cross, requested) : Size(requested, cross));
        const float extent = vertical ? result.width : result.height;
        const float mainExtent = vertical ? result.height : result.width;
        const float free = mainExtent > main ? mainExtent - main : 0;
        float position = main_ == MainAlign::center ? free * 0.5f : main_ == MainAlign::end ? free : 0;
        const float spacing = gap_ + (main_ == MainAlign::spaceBetween && childCount() > 1
            ? free / static_cast<float>(childCount() - 1) : 0);
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            const float childCross = vertical ? child->size().width : child->size().height;
            float offset = 0;
            if (align_ == CrossAlign::center) offset = (extent - childCross) * 0.5f;
            else if (align_ == CrossAlign::end) offset = extent - childCross;
            child->setOffset(vertical ? Offset(offset, position) : Offset(position, offset));
            position += (vertical ? child->size().height : child->size().width) + spacing;
        }
        return result;
    }
private:
    Axis axis_ = Axis::vertical;
    float gap_ = 0;
    CrossAlign align_ = CrossAlign::start;
    MainAlign main_ = MainAlign::start;
};

class Linear : public RenderWidget {
public:
    Linear(ChildBuilder children, Axis axis, float gap = 0,
           CrossAlign align = CrossAlign::start, Key key = {}, MainAlign main = MainAlign::start) noexcept
        : RenderWidget(typeId<Linear>(), key), children_(children), axis_(axis), gap_(gap), align_(align), main_(main) {}
    BuildResult build(BuildContext& context) const noexcept override { return children_.build(context); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderLinear>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& object) const noexcept override {
        if (!std::isfinite(gap_) || gap_ < 0) return UiError::invalidInput;
        static_cast<RenderLinear&>(object).configure(axis_, gap_, align_, main_);
        return UiError::none;
    }
private:
    ChildBuilder children_;
    Axis axis_;
    float gap_;
    CrossAlign align_;
    MainAlign main_;
};

class RenderAlign final : public RenderObject {
public:
    void configure(float x, float y) noexcept { x_ = x; y_ = y; markNeedsLayout(); }
protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        Size extent;
        const BoxConstraints loose(0, c.maxWidth, 0, c.maxHeight);
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            child->layout(loose);
            if (child->size().width > extent.width) extent.width = child->size().width;
            if (child->size().height > extent.height) extent.height = child->size().height;
        }
        const auto size = c.constrain(Size(std::isfinite(c.maxWidth) ? c.maxWidth : extent.width,
            std::isfinite(c.maxHeight) ? c.maxHeight : extent.height));
        for (auto* child = firstChild(); child; child = child->nextSibling())
            child->setOffset(Offset(std::floor((size.width - child->size().width) * x_),
                std::floor((size.height - child->size().height) * y_)));
        return size;
    }
private:
    float x_ = 0.5f, y_ = 0.5f;
};
class Align final : public RenderWidget {
public:
    Align(ChildBuilder child, float x = 0.5f, float y = 0.5f, Key key = {}) noexcept
        : RenderWidget(typeId<Align>(), key), child_(child), x_(x), y_(y) {}
    BuildResult build(BuildContext& c) const noexcept override { return child_.build(c); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderAlign>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        if (!std::isfinite(x_) || !std::isfinite(y_) || x_ < 0 || x_ > 1 || y_ < 0 || y_ > 1)
            return UiError::invalidInput;
        static_cast<RenderAlign&>(r).configure(x_, y_); return UiError::none;
    }
private:
    ChildBuilder child_;
    float x_, y_;
};

class RenderPadding final : public RenderObject {
public:
    void configure(Insets value) noexcept { insets_ = value; markNeedsLayout(); }
protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        const float width = insets_.left + insets_.right, height = insets_.top + insets_.bottom;
        const BoxConstraints inner(c.minWidth > width ? c.minWidth - width : 0,
                                   c.maxWidth > width ? c.maxWidth - width : 0,
                                   c.minHeight > height ? c.minHeight - height : 0,
                                   c.maxHeight > height ? c.maxHeight - height : 0);
        Size extent;
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            child->layout(inner);
            child->setOffset(Offset(insets_.left, insets_.top));
            if (child->size().width > extent.width) extent.width = child->size().width;
            if (child->size().height > extent.height) extent.height = child->size().height;
        }
        return Size(extent.width + width, extent.height + height);
    }
private:
    Insets insets_;
};
class Padding final : public RenderWidget {
public:
    Padding(Insets value, ChildBuilder child, Key key = {}) noexcept
        : RenderWidget(typeId<Padding>(), key), insets_(value), child_(child) {}
    BuildResult build(BuildContext& c) const noexcept override { return child_.build(c); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderPadding>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        if (!insets_.valid()) return UiError::invalidInput;
        static_cast<RenderPadding&>(r).configure(insets_); return UiError::none;
    }
private:
    Insets insets_;
    ChildBuilder child_;
};

class RenderSizedBox final : public RenderObject {
public:
    void configure(Size size) noexcept { desired_ = size; markNeedsLayout(); }
protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        const auto result = c.constrain(desired_);
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            child->layout(BoxConstraints::tight(result)); child->setOffset({});
        }
        return result;
    }
private:
    Size desired_;
};
class SizedBox final : public RenderWidget {
public:
    SizedBox(Size size, ChildBuilder child = {}, Key key = {}) noexcept
        : RenderWidget(typeId<SizedBox>(), key), size_(size), child_(child) {}
    BuildResult build(BuildContext& c) const noexcept override { return child_.build(c); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderSizedBox>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        if (!std::isfinite(size_.width) || !std::isfinite(size_.height) || size_.width < 0 || size_.height < 0)
            return UiError::invalidInput;
        static_cast<RenderSizedBox&>(r).configure(size_); return UiError::none;
    }
private:
    Size size_;
    ChildBuilder child_;
};

class RenderScroll final : public RenderObject {
public:
    void configure(float offset, float height) noexcept { offset_ = offset; height_ = height; markNeedsLayout(); }
    float effectiveOffset() const noexcept { return effective_; }
protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        const float height = c.constrain(Size(0, height_)).height;
        const BoxConstraints content(0, c.maxWidth, 0, std::numeric_limits<float>::infinity());
        float width = 0, extent = 0;
        for (auto* child = firstChild(); child; child = child->nextSibling()) {
            child->layout(content);
            if (child->size().width > width) width = child->size().width;
            if (child->size().height > extent) extent = child->size().height;
        }
        const float maximum = extent > height ? extent - height : 0;
        effective_ = offset_ > maximum ? maximum : offset_;
        for (auto* child = firstChild(); child; child = child->nextSibling())
            child->setOffset(Offset(0, -effective_));
        return Size(width, height);
    }
private:
    float offset_ = 0, height_ = 0, effective_ = 0;
};
class Scroll final : public RenderWidget {
public:
    Scroll(float offset, float height, ChildBuilder child, Key key = {}) noexcept
        : RenderWidget(typeId<Scroll>(), key), offset_(offset), height_(height), child_(child) {}
    BuildResult build(BuildContext& c) const noexcept override { return child_.build(c); }
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderScroll>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        if (!std::isfinite(offset_) || !std::isfinite(height_) || offset_ < 0 || height_ < 0)
            return UiError::invalidInput;
        static_cast<RenderScroll&>(r).configure(offset_, height_); return UiError::none;
    }
private:
    float offset_, height_;
    ChildBuilder child_;
};
} // namespace NanoUI
