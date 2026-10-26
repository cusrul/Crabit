#pragma once

#include <NanoUI/framework/widget.h>

namespace NanoUI {

// Callback-built configurations, owned once. Nodes and widgets use the caller's
// allocator; failed append retains the caller's WidgetPtr and the existing list.
class WidgetList {
public:
    struct Node {
        explicit Node(WidgetPtr&& value) noexcept : widget(std::move(value)) {}
        WidgetPtr widget;
        Owned<Node> next;
    };
    WidgetList() noexcept = default;
    WidgetList(WidgetList&& other) noexcept : head_(std::move(other.head_)) {}
    WidgetList& operator=(WidgetList&& other) noexcept {
        head_ = std::move(other.head_); return *this;
    }
    UiError append(Allocator& allocator, WidgetPtr& value) noexcept {
        if (!value) return UiError::invalidInput;
        auto node = makeOwned<Node>(allocator, AllocationKind::container, std::move(value));
        if (!node) return UiError::outOfMemory;
        Owned<Node>* tail = &head_;
        while (*tail) tail = &(*tail)->next;
        *tail = std::move(node);
        return UiError::none;
    }
    Node* first() const noexcept { return head_.get(); }
    bool empty() const noexcept { return !head_; }
private:
    Owned<Node> head_;
};

} // namespace NanoUI
