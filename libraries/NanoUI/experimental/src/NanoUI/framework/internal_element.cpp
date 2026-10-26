#include <NanoUI/framework/internal_element.h>

#include <limits>
#include <utility>

namespace NanoUI { namespace detail {

BuildOwnerImpl::BuildOwnerImpl(Allocator& allocatorValue, BuildOwner& publicOwnerValue,
                               OwnerOptions optionsValue) noexcept
    : allocator(allocatorValue), publicOwner(publicOwnerValue), options(optionsValue) {}

BuildOwnerImpl::~BuildOwnerImpl() noexcept {
    OperationPhaseGuard operation(phase, CallbackPhase::operation);
    if (root) {
        invalidateSubtree(*root);
        disposeSubtree(*root);
        root.reset();
    }
}

Element* BuildOwnerImpl::find(ElementHandle handle) const noexcept {
    if (handle.owner != &publicOwner || handle.serial == 0) return nullptr;
    for (Element* item = registry; item; item = item->registryNext) {
        if (item->serial == handle.serial && item->phase != ElementPhase::unmounted)
            return item;
    }
    return nullptr;
}

void BuildOwnerImpl::registerElement(Element& element) noexcept {
    element.registryNext = registry;
    registry = &element;
    element.registered = true;
}

void BuildOwnerImpl::unregisterElement(Element& element) noexcept {
    if (!element.registered) return;
    Element** link = &registry;
    while (*link && *link != &element) link = &(*link)->registryNext;
    if (*link == &element) *link = element.registryNext;
    element.registryNext = nullptr;
    element.registered = false;
}

bool BuildOwnerImpl::newTransaction(std::uint64_t& value) noexcept {
    if (transactionExhausted || nextTransaction == 0) return false;
    value = nextTransaction;
    if (nextTransaction == std::numeric_limits<std::uint64_t>::max())
        transactionExhausted = true;
    else
        ++nextTransaction;
    return true;
}

UiError BuildOwnerImpl::mount(WidgetPtr&& widgetValue, std::size_t depth,
                              std::uint64_t transaction,
                              Owned<Element>& result) noexcept {
    if (!widgetValue) return UiError::invalidInput;
    if (depth > options.maxDepth) return UiError::capacityExceeded;
    if (builds >= options.maxBuildsPerOperation) return UiError::capacityExceeded;
    if (serialExhausted || nextSerial == 0) return UiError::capacityExceeded;

    // Keep ownership in a local until the caller receives it. Registration is
    // removed on allocation failure only when no Element was created.
    const std::uint64_t serial = nextSerial;
    if (nextSerial == std::numeric_limits<std::uint64_t>::max()) serialExhausted = true;
    else ++nextSerial;
    auto element = makeOwned<Element>(allocator, AllocationKind::element,
                                      *this, serial, std::move(widgetValue), transaction);
    if (!element) return UiError::outOfMemory;
    registerElement(*element);

    if (element->widget->kind() == WidgetKind::stateful) {
        {
            OperationPhaseGuard callbackPhase(phase, CallbackPhase::init);
            element->state = static_cast<const StatefulWidget&>(*element->widget).createState(allocator);
        }
        if (!element->state) {
            result = std::move(element);
            return UiError::outOfMemory;
        }
        element->initialized = true;
        BuildContext callbackContext(*element->widget,
            ElementHandle{&publicOwner, element->serial}, allocator, publicOwner);
        {
            OperationPhaseGuard callbackPhase(phase, CallbackPhase::init);
            element->state->initState(callbackContext);
        }
    }

    const UiError built = attempt(*element, depth, transaction, true);
    result = std::move(element);
    return built;
}

UiError BuildOwnerImpl::attempt(Element& element, std::size_t depth,
                                std::uint64_t transaction, bool staged) noexcept {
    if (builds >= options.maxBuildsPerOperation) return UiError::capacityExceeded;
    ++builds;
    if (element.widget->kind() == WidgetKind::rendering) {
        OperationPhaseGuard callbackPhase(phase, CallbackPhase::build);
        const auto& widget = static_cast<const RenderWidget&>(*element.widget);
        if (!element.render) {
            element.render = widget.createRenderObject(allocator);
            renderLinksDirty = true;
            if (!element.render) return UiError::outOfMemory;
            if (element.render->parent() || element.render->childCount() != 0)
                return UiError::invalidInput;
        }
        const auto updated = widget.updateRenderObject(*element.render);
        if (updated != UiError::none) return updated;
    }
    BuildResult result{};
    {
        OperationPhaseGuard callbackPhase(phase, CallbackPhase::build);
        result = element.invokeBuild();
    }

    if (result.error != UiError::none) {
        result.child.reset();
        return result.error;
    }
    if (result.child && !result.children.empty()) return UiError::invalidInput;
    const UiError reconciled = result.children.empty()
        ? reconcileChild(element, std::move(result.child), depth + 1, transaction, staged)
        : reconcileChildren(element, result.children, depth + 1, transaction, staged);
    if (reconciled == UiError::none) {
        element.dirty = false;
        renderLinksDirty = true;
    }
    return reconciled;
}

void BuildOwnerImpl::updateCompatible(Element& element, WidgetPtr&& replacement) noexcept {
    WidgetPtr previousWidget = std::move(element.widget);
    element.widget = std::move(replacement);
    if (element.state) {
        BuildContext callbackContext(*element.widget,
            ElementHandle{&publicOwner, element.serial}, allocator, publicOwner);
        OperationPhaseGuard callbackPhase(phase, CallbackPhase::update);
        element.state->didUpdateWidget(*previousWidget, callbackContext);
    }
    element.dirty = true;
    // previousWidget deliberately remains alive through didUpdateWidget.
}

UiError BuildOwnerImpl::reconcileChild(Element& parent, WidgetPtr&& childWidget,
                                       std::size_t depth, std::uint64_t transaction,
                                       bool staged) noexcept {
    if (childWidget && options.maxChildren == 0) return UiError::capacityExceeded;
    // Retain the original single-child fast path; switching out of a list uses
    // the same transactional reconciler rather than deleting sibling States.
    if (parent.child && parent.child->sibling) {
        WidgetList configs;
        if (childWidget) {
            const auto result = configs.append(allocator, childWidget);
            if (result != UiError::none) return result;
        }
        return reconcileChildren(parent, configs, depth, transaction, staged);
    }
    if (!childWidget) {
        if (parent.child) {
            if (staged) {
                invalidateSubtree(*parent.child);
                disposeSubtree(*parent.child);
            } else {
                invalidateSubtree(*parent.child);
                disposeSubtree(*parent.child);
            }
            parent.child.reset();
        }
        return UiError::none;
    }

    if (parent.child && parent.child->widget->type() == childWidget->type() &&
        parent.child->widget->key() == childWidget->key()) {
        if (staged) return UiError::invalidInput; // A new staged parent has no prior child.
        updateCompatible(*parent.child, std::move(childWidget));
        return UiError::none;
    }

    if (staged) {
        Owned<Element> stagedChild;
        const UiError mounted = mount(std::move(childWidget), depth, transaction, stagedChild);
        parent.child = std::move(stagedChild);
        return mounted;
    }

    std::uint64_t childTransaction = 0;
    if (!newTransaction(childTransaction)) return UiError::capacityExceeded;
    Owned<Element> stagedChild;
    const UiError mounted = mount(std::move(childWidget), depth, childTransaction, stagedChild);
    if (mounted != UiError::none) {
        rollback(stagedChild, childTransaction);
        return mounted;
    }
    commitTransaction(childTransaction);
    if (parent.child) {
        invalidateSubtree(*parent.child);
        disposeSubtree(*parent.child);
    }
    parent.child = std::move(stagedChild);
    if (parent.child) setTreePhase(*parent.child, rootActive ? ElementPhase::active
                                                            : ElementPhase::inactive);
    return UiError::none;
}

void BuildOwnerImpl::invalidateTransaction(std::uint64_t transaction) noexcept {
    for (Element* item = registry; item;) {
        Element* next = item->registryNext;
        if (item->transaction == transaction) {
            unregisterElement(*item);
            item->phase = ElementPhase::unmounted;
        }
        item = next;
    }
}

void BuildOwnerImpl::invalidateSubtree(Element& element) noexcept {
    for (auto* child = element.child.get(); child; child = child->sibling.get())
        invalidateSubtree(*child);
    unregisterElement(element);
    element.phase = ElementPhase::unmounted;
}

void BuildOwnerImpl::disposeSubtree(Element& element) noexcept {
    while (element.child) {
        Owned<Element> child = std::move(element.child);
        element.child = std::move(child->sibling);
        disposeSubtree(*child);
    }
    if (element.state && element.initialized) {
        element.initialized = false;
        OperationPhaseGuard callbackPhase(phase, CallbackPhase::dispose);
        element.state->dispose();
    }
    element.state.reset();
    element.render.reset();
    element.widget.reset();
}

void BuildOwnerImpl::rollback(Owned<Element>& stagedRoot,
                              std::uint64_t transaction) noexcept {
    invalidateTransaction(transaction);
    if (stagedRoot) {
        // Invalidate any descendants defensively before the first dispose hook.
        invalidateSubtree(*stagedRoot);
        disposeSubtree(*stagedRoot);
        stagedRoot.reset();
    }
}

void BuildOwnerImpl::commitTransaction(std::uint64_t transaction) noexcept {
    for (Element* item = registry; item; item = item->registryNext) {
        if (item->transaction == transaction) {
            item->transaction = 0;
            item->phase = rootActive ? ElementPhase::active : ElementPhase::inactive;
        }
    }
}

void BuildOwnerImpl::setTreePhase(Element& element, ElementPhase newPhase) noexcept {
    element.phase = newPhase;
    for (auto* child = element.child.get(); child; child = child->sibling.get())
        setTreePhase(*child, newPhase);
}

bool BuildOwnerImpl::hasActiveDirty(const Element& element) const noexcept {
    if (element.phase == ElementPhase::active && element.dirty) return true;
    for (const auto* child = element.child.get(); child; child = child->sibling.get())
        if (hasActiveDirty(*child)) return true;
    return false;
}

UiError BuildOwnerImpl::linkRenderTree(Element& element, RenderObject& parent) noexcept {
    RenderObject* next = &parent;
    if (element.render) {
        if (!parent.attachBorrowed(*element.render)) return UiError::capacityExceeded;
        next = element.render.get();
    }
    for (auto* child = element.child.get(); child; child = child->sibling.get()) {
        const auto result = linkRenderTree(*child, *next);
        if (result != UiError::none) return result;
    }
    return UiError::none;
}

} } // namespace NanoUI::detail
