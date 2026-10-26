#pragma once

#include "paint_canvas.h"
#include <NanoUI/foundation/owned.h>
#include <cstddef>
#include <utility>

namespace NanoUI {

class RenderObject;
enum class InputAction { activate, back, next, previous };
struct HitTestResult {
    // Borrowed for immediate event callback only; never retain across mutation.
    RenderObject* target;
    Offset localPosition;
    HitTestResult(RenderObject* object = nullptr, Offset position = Offset()) noexcept
        : target(object), localPosition(position) {}
    explicit operator bool() const noexcept { return target != nullptr; }
};

// Single UI context. Standalone trees own children; Element trees borrow children
// through explicit attachment. Each parent uses exactly one mode. Traversal
// callbacks must not append/detach/destroy nodes during layout/paint/hitTest.
class RenderObject {
public:
    enum : std::size_t { maxDepth = 64 }; // Header-only C++11, no ODR storage.
    RenderObject() noexcept = default;
    RenderObject(const RenderObject&) = delete;
    RenderObject& operator=(const RenderObject&) = delete;
    virtual ~RenderObject() noexcept {
        // Tear down siblings iteratively, not recursively through next_.
        if (parent_ && !parent_->first_) parent_->unlinkBorrowed(*this);
        while (first_) { auto child = detach(*first_); child.reset(); }
        clearBorrowedChildren();
    }
    RenderObject* parent() const noexcept { return parent_; }
    RenderObject* firstChild() const noexcept { return linkedFirst_; }
    RenderObject* lastChild() const noexcept { return last_; }
    RenderObject* nextSibling() const noexcept { return linkedNext_; }
    RenderObject* previousSibling() const noexcept { return previous_; }
    std::size_t childCount() const noexcept { return childCount_; }
    virtual bool handleInput(InputAction) noexcept { return false; }
    virtual bool canFocus() const noexcept { return false; }
    virtual void focusChanged(bool) noexcept {}

    template<class T> bool append(Owned<T>& child) noexcept {
        static_assert(std::is_convertible<T*, RenderObject*>::value, "RenderObject child required");
        RenderObject* node = child.get();
        if (!node || (linkedFirst_ && !first_) || !canAttach(*node)) return false;
        node->parent_ = this;
        node->previous_ = last_;
        if (last_) { last_->linkedNext_ = node; last_->next_ = std::move(child); }
        else { linkedFirst_ = node; first_ = std::move(child); }
        last_ = node;
        ++childCount_;
        markNeedsLayout();
        return true;
    }
    // Element owns these nodes. One parent cannot mix borrowed and owned children.
    bool attachBorrowed(RenderObject& node) noexcept {
        if (first_ || !canAttach(node)) return false;
        node.parent_ = this;
        node.previous_ = last_;
        if (last_) last_->linkedNext_ = &node;
        else linkedFirst_ = &node;
        last_ = &node;
        ++childCount_;
        markNeedsLayout();
        return true;
    }
    bool unlinkBorrowed(RenderObject& node) noexcept {
        if (first_ || node.parent_ != this) return false;
        unlink(node);
        return true;
    }
    void clearBorrowedChildren() noexcept {
        if (first_) return;
        while (linkedFirst_) unlink(*linkedFirst_);
    }
private:
    bool canAttach(const RenderObject& candidate) const noexcept {
        const auto* node = &candidate;
        if (node->parent_ || node->previous_ || node->linkedNext_ || node->next_) return false;
        std::size_t depth = 0;
        for (auto* p = this; p; p = p->parent_) {
            if (p == node) return false;
            ++depth;
        }
        // Walk the candidate subtree without allocations or recursion.
        std::size_t level = 1;
        for (auto* p = node; p;) {
            if (depth + level > maxDepth) return false;
            if (p->linkedFirst_) { p = p->linkedFirst_; ++level; continue; }
            while (p != node && !p->linkedNext_) { p = p->parent_; --level; }
            if (p == node) break;
            p = p->linkedNext_;
        }
        return true;
    }
    void unlink(RenderObject& child) noexcept {
        if (child.previous_) child.previous_->linkedNext_ = child.linkedNext_;
        else linkedFirst_ = child.linkedNext_;
        if (child.linkedNext_) child.linkedNext_->previous_ = child.previous_;
        else last_ = child.previous_;
        child.parent_ = nullptr;
        child.previous_ = nullptr;
        child.linkedNext_ = nullptr;
        --childCount_;
        markNeedsLayout();
    }
public:
    Owned<RenderObject> detach(RenderObject& child) noexcept {
        if (!first_ || child.parent_ != this) return {};
        auto& slot = child.previous_ ? child.previous_->next_ : first_;
        Owned<RenderObject> result = std::move(slot);
        slot = std::move(child.next_);
        unlink(child);
        return result;
    }

    Size size() const noexcept { return size_; }
    Offset offset() const noexcept { return offset_; }
    Offset localToGlobal(Offset position) const noexcept {
        for (auto* p = this; p; p = p->parent_) position = position + p->offset_;
        return position;
    }
    Offset globalToLocal(Offset position) const noexcept {
        for (auto* p = this; p; p = p->parent_) position = position - p->offset_;
        return position;
    }
    const BoxConstraints& constraints() const noexcept { return constraints_; }
    bool setOffset(Offset value) noexcept {
        if (!value.isFinite()) return false;
        if (offset_.dx != value.dx || offset_.dy != value.dy) {
            offset_ = value;
            markNeedsPaint();
        }
        return true;
    }
    bool needsLayout() const noexcept { return needsLayout_; }
    bool needsPaint() const noexcept { return needsPaint_; }
    void markNeedsLayout() noexcept {
        for (auto* p = this; p; p = p->parent_) { p->needsLayout_ = true; p->needsPaint_ = true; }
    }
    void markNeedsPaint() noexcept {
        for (auto* p = this; p; p = p->parent_) p->needsPaint_ = true;
    }
    bool layout(const BoxConstraints& constraints) noexcept {
        if (!constraints.isNormalized()) return false;
        if (!needsLayout_ && constraints == constraints_) return true;
        constraints_ = constraints;
        needsLayout_ = false; // Invalidations made by a callback remain pending.
        size_ = constraints.constrain(performLayout(constraints));
        markNeedsPaint();
        return true;
    }
    // Full repaint of this subtree, even if clean: invalidated overlap requires
    // repainting clean siblings too. Layout must have completed before painting.
    void paint(PaintCanvas& canvas) noexcept {
        if (needsLayout_) return;
        needsPaint_ = false;
        canvas.save();
        canvas.translate(offset_);
        canvas.clipRect(Rect::fromSize(size_));
        performPaint(canvas);
        for (auto* child = linkedFirst_; child; child = child->linkedNext_) child->paint(canvas);
        canvas.restore();
    }
    // Input position is in this object's local coordinates (not parent's).
    HitTestResult hitTest(Offset localPosition) noexcept {
        if (needsLayout_ || !Rect::fromSize(size_).contains(localPosition)) return {};
        for (auto* child = last_; child; child = child->previous_) {
            auto hit = child->hitTest(localPosition - child->offset_);
            if (hit) return hit;
        }
        if (hitTestSelf(localPosition)) return {this, localPosition};
        return {};
    }
protected:
    // Default container gives children loose constraints and takes their extent.
    virtual Size performLayout(const BoxConstraints& constraints) noexcept {
        Size extent;
        const BoxConstraints loose(0, constraints.maxWidth, 0, constraints.maxHeight);
        for (auto* child = linkedFirst_; child; child = child->linkedNext_) {
            child->layout(loose);
            const float w = child->offset_.dx + child->size_.width;
            const float h = child->offset_.dy + child->size_.height;
            if (w > extent.width) extent.width = w;
            if (h > extent.height) extent.height = h;
        }
        return extent;
    }
    virtual void performPaint(PaintCanvas&) noexcept {}
    virtual bool hitTestSelf(Offset) const noexcept { return true; }
private:
    Owned<RenderObject> first_, next_;
    RenderObject* linkedFirst_ = nullptr;
    RenderObject* linkedNext_ = nullptr;
    RenderObject* parent_ = nullptr;
    RenderObject* previous_ = nullptr;
    RenderObject* last_ = nullptr;
    std::size_t childCount_ = 0;
    Size size_;
    Offset offset_;
    BoxConstraints constraints_;
    bool needsLayout_ = true, needsPaint_ = true;
};

} // namespace NanoUI
