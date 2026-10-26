#include <NanoUI/framework/internal_element.h>

namespace NanoUI {
namespace {
using detail::Element;
bool contains(const Element& root, const Element& target) noexcept {
    if (&root == &target) return true;
    for (auto* child = root.child.get(); child; child = child->sibling.get())
        if (contains(*child, target)) return true;
    return false;
}
bool eligible(const Element* e) noexcept {
    return e && e->phase == ElementPhase::active && e->render && e->render->canFocus();
}
struct Traversal {
    Element* current;
    Element* first = nullptr;
    Element* last = nullptr;
    Element* before = nullptr;
    Element* after = nullptr;
    bool seen = false;
    explicit Traversal(Element* value) noexcept : current(value) {}
    void visit(Element& e) noexcept {
        if (eligible(&e)) {
            if (!first) first = &e;
            if (&e == current) { before = last; seen = true; }
            else if (seen && !after) after = &e;
            last = &e;
        }
        for (auto* child = e.child.get(); child; child = child->sibling.get()) visit(*child);
    }
};
} // namespace

detail::Element* detail::BuildOwnerImpl::scopeRoot() const noexcept {
    return inputScope.serial ? find(inputScope) : root.get();
}
bool detail::BuildOwnerImpl::withinInputScope(const Element& e) const noexcept {
    const auto* scope = scopeRoot();
    return scope && scope->phase == ElementPhase::active && contains(*scope, e);
}
ElementHandle BuildOwner::focus() const noexcept {
    if (!impl_ || !impl_->rootActive) return {};
    auto* e = impl_->find(impl_->focused);
    return eligible(e) && impl_->withinInputScope(*e) ? impl_->focused : ElementHandle{};
}
UiError BuildOwner::setFocus(ElementHandle handle) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    if (!owner.rootActive) return UiError::invalidInput;
    Element* next = nullptr;
    if (handle.serial || handle.owner) {
        next = owner.find(handle);
        if (!next) return UiError::staleHandle;
        if (!eligible(next) || !owner.withinInputScope(*next)) return UiError::invalidInput;
    }
    auto* old = owner.find(owner.focused);
    if (old == next) return UiError::none;
    detail::OperationPhaseGuard guard(owner.phase, detail::CallbackPhase::input);
    owner.focused = next ? handle : ElementHandle{};
    if (old && old->render) old->render->focusChanged(false);
    if (next) next->render->focusChanged(true);
    return UiError::none;
}
UiError BuildOwner::focusNext(bool forward, bool wrap) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    auto* scope = owner.scopeRoot();
    if (!owner.rootActive || !scope || scope->phase != ElementPhase::active) return UiError::invalidInput;
    Traversal scan(owner.find(focus()));
    scan.visit(*scope);
    auto* next = forward ? scan.after : scan.before;
    if (!scan.seen || (!next && wrap)) next = forward ? scan.first : scan.last;
    if (!next) return scan.seen ? UiError::none : UiError::invalidInput;
    return setFocus(ElementHandle(this, next->serial));
}
UiError BuildOwner::dispatchFocused(InputAction action) noexcept {
    if (!impl_) return UiError::outOfMemory;
    if (impl_->phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    if (action == InputAction::next) return focusNext();
    if (action == InputAction::previous) return focusNext(false);
    const auto target = focus();
    return target.serial ? dispatchInput(target, action) : UiError::invalidInput;
}
UiError BuildOwner::setInputScope(ElementHandle handle) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    if (!owner.rootActive) return UiError::invalidInput;
    if (handle.serial || handle.owner) {
        const auto* e = owner.find(handle);
        if (!e) return UiError::staleHandle;
        if (e->phase != ElementPhase::active) return UiError::invalidInput;
    }
    owner.inputScope = handle;
    auto* old = owner.find(owner.focused);
    if (old && !owner.withinInputScope(*old)) return setFocus({});
    return UiError::none;
}
ElementHandle BuildOwner::inputScope() const noexcept {
    if (!impl_ || !impl_->rootActive) return {};
    const auto* e = impl_->scopeRoot();
    return e && e->phase == ElementPhase::active ? ElementHandle(this, e->serial) : ElementHandle{};
}
} // namespace NanoUI
