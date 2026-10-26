#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/internal_element.h>
#include <NanoUI/framework/widget.h>

#include <utility>

namespace NanoUI {

BuildOwner::BuildOwner(Allocator& allocator, OwnerOptions options) noexcept
    : impl_(makeOwned<detail::BuildOwnerImpl>(allocator, AllocationKind::container,
                                               allocator, *this, options)) {}

BuildOwner::~BuildOwner() noexcept = default;

UiError BuildOwner::setRoot(WidgetPtr widget) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::operation);
    // Error cleanup must run before this guard restores idle; the by-value
    // parameter is left empty so its later destruction cannot reenter unguarded.
    Owned<Widget> config = std::move(widget);
    if (!config) return UiError::invalidInput;
    if (owner.options.maxDepth == 0 || owner.options.maxBuildsPerOperation == 0)
        return UiError::invalidInput;
    owner.builds = 0;

    if (owner.root && owner.root->widget->type() == config->type() &&
        owner.root->widget->key() == config->key()) {
        owner.updateCompatible(*owner.root, std::move(config));
        return UiError::none;
    }

    std::uint64_t transaction = 0;
    if (!owner.newTransaction(transaction)) return UiError::capacityExceeded;
    Owned<detail::Element> staged;
    const UiError mounted = owner.mount(std::move(config), 1, transaction, staged);
    if (mounted != UiError::none) {
        owner.rollback(staged, transaction);
        return mounted;
    }

    owner.commitTransaction(transaction);
    owner.setTreePhase(*staged, owner.rootActive ? ElementPhase::active
                                                  : ElementPhase::inactive);
    Owned<detail::Element> previous = std::move(owner.root);
    owner.root = std::move(staged);
    if (previous) {
        owner.invalidateSubtree(*previous);
        owner.disposeSubtree(*previous);
        previous.reset();
    }
    return UiError::none;
}

UiError BuildOwner::clearRoot() noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    if (owner.options.maxDepth == 0 || owner.options.maxBuildsPerOperation == 0)
        return UiError::invalidInput;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::operation);
    Owned<detail::Element> previous = std::move(owner.root);
    if (previous) {
        owner.invalidateSubtree(*previous);
        owner.disposeSubtree(*previous);
        previous.reset();
    }
    return UiError::none;
}

ElementHandle BuildOwner::root() const noexcept {
    if (!impl_ || !impl_->root) return {};
    return ElementHandle{this, impl_->root->serial};
}

SnapshotResult BuildOwner::inspect(ElementHandle handle) const noexcept {
    SnapshotResult result{};
    if (!impl_) {
        result.error = UiError::staleHandle;
        return result;
    }
    const detail::Element* element = impl_->find(handle);
    if (!element) {
        result.error = UiError::staleHandle;
        return result;
    }
    result.value.phase = element->phase;
    result.value.type = element->widget->type();
    result.value.key = element->widget->key();
    result.value.child = element->child
        ? ElementHandle{this, element->child->serial} : ElementHandle{};
    result.value.dirty = element->dirty;
    result.value.hasState = static_cast<bool>(element->state);
    result.value.focusable = element->phase == ElementPhase::active && element->render && element->render->canFocus();
    for (const auto* item = element->child.get(); item; item = item->sibling.get())
        ++result.value.childCount;
    return result;
}

ElementHandle BuildOwner::childAt(ElementHandle handle, std::size_t index) const noexcept {
    if (!impl_) return {};
    const auto* element = impl_->find(handle);
    if (!element) return {};
    const auto* child = element->child.get();
    while (child && index != 0) { child = child->sibling.get(); --index; }
    return child ? ElementHandle{this, child->serial} : ElementHandle{};
}

UiError BuildOwner::setState(ElementHandle handle, StateMutation mutation,
                             void* payload) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle && owner.phase != detail::CallbackPhase::input)
        return UiError::invalidPhase;
    if (owner.options.maxDepth == 0 || owner.options.maxBuildsPerOperation == 0)
        return UiError::invalidInput;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::operation);
    detail::Element* element = owner.find(handle);
    if (!element) return UiError::staleHandle;
    if (!mutation || !element->state) return UiError::invalidInput;
    detail::OperationPhaseGuard callbackPhase(owner.phase, detail::CallbackPhase::update);
    mutation(*element->state, payload);
    element->dirty = true;
    return UiError::none;
}

UiError BuildOwner::requestBuild(ElementHandle handle) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle && owner.phase != detail::CallbackPhase::input)
        return UiError::invalidPhase;
    if (owner.options.maxDepth == 0 || owner.options.maxBuildsPerOperation == 0)
        return UiError::invalidInput;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::operation);
    detail::Element* element = owner.find(handle);
    if (!element) return UiError::staleHandle;
    element->dirty = true;
    return UiError::none;
}

UiError BuildOwner::setRootActive(bool active) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    if (owner.options.maxDepth == 0 || owner.options.maxBuildsPerOperation == 0)
        return UiError::invalidInput;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::operation);
    // Another route may have painted the shared surface while this tree was hidden.
    if (active && !owner.rootActive) owner.viewport.markNeedsPaint();
    owner.rootActive = active;
    if (owner.root) owner.setTreePhase(*owner.root, active ? ElementPhase::active
                                                          : ElementPhase::inactive);
    return UiError::none;
}

PumpResult BuildOwner::pump() noexcept {
    PumpResult result{};
    if (!impl_) {
        result.error = UiError::outOfMemory;
        return result;
    }
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) {
        result.error = UiError::invalidPhase;
        return result;
    }
    if (owner.options.maxDepth == 0 || owner.options.maxBuildsPerOperation == 0) {
        result.error = UiError::invalidInput;
        return result;
    }
    owner.builds = 0;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::pump);
    if (owner.root) result.error = owner.pumpTree(*owner.root, 1);
    result.builds = owner.builds;
    result.needsFrame = owner.root && owner.rootActive && owner.hasActiveDirty(*owner.root);
    return result;
}

UiError BuildOwner::dispatchInput(ElementHandle handle, InputAction action) noexcept {
    if (!impl_) return UiError::outOfMemory;
    auto& owner = *impl_;
    if (owner.phase != detail::CallbackPhase::idle) return UiError::invalidPhase;
    auto* element = owner.find(handle);
    if (!element) return UiError::staleHandle;
    if (element->phase != ElementPhase::active || !element->render) return UiError::invalidInput;
    if (!owner.withinInputScope(*element)) return UiError::invalidInput;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::input);
    return element->render->handleInput(action) ? UiError::none : UiError::invalidInput;
}

bool BuildOwner::atSafePoint() const noexcept {
    return impl_ && impl_->phase == detail::CallbackPhase::idle;
}

} // namespace NanoUI
