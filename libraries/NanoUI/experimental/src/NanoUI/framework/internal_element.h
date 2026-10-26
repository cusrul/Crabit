#pragma once

#include <NanoUI/framework/build_context.h>
#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/state.h>
#include <NanoUI/framework/widget.h>
#include <NanoUI/rendering/render_object.h>

namespace NanoUI { namespace detail {

class BuildOwnerImpl;

enum class CallbackPhase { idle, operation, init, update, build, dispose, pump, frame, input };

class OperationPhaseGuard final {
public:
    OperationPhaseGuard(CallbackPhase& phase, CallbackPhase next) noexcept
        : phase_(phase), previous_(phase) { phase_ = next; }
    ~OperationPhaseGuard() noexcept { phase_ = previous_; }
    OperationPhaseGuard(const OperationPhaseGuard&) = delete;
    OperationPhaseGuard& operator=(const OperationPhaseGuard&) = delete;
private:
    CallbackPhase& phase_;
    CallbackPhase previous_;
};

class Element final {
public:
    Element(BuildOwnerImpl&, std::uint64_t, WidgetPtr&&, std::uint64_t) noexcept;
    ~Element() noexcept = default;
    Element(const Element&) = delete;
    Element& operator=(const Element&) = delete;

    BuildResult invokeBuild() noexcept;
    void updateWidget(WidgetPtr&&) noexcept;
    void setPhase(ElementPhase) noexcept;

    BuildOwnerImpl* owner;
    std::uint64_t serial;
    std::uint64_t transaction;
    ElementPhase phase = ElementPhase::created;
    WidgetPtr widget;
    Owned<State> state;
    Owned<RenderObject> render;
    Owned<Element> child;
    Owned<Element> sibling;
    Element* registryNext = nullptr;
    bool registered = false;
    bool initialized = false;
    bool dirty = true;
};

class BuildOwnerImpl final {
public:
    BuildOwnerImpl(Allocator&, BuildOwner&, OwnerOptions) noexcept;
    ~BuildOwnerImpl() noexcept;
    BuildOwnerImpl(const BuildOwnerImpl&) = delete;
    BuildOwnerImpl& operator=(const BuildOwnerImpl&) = delete;

    Allocator& allocator;
    BuildOwner& publicOwner;
    OwnerOptions options;
    Owned<Element> root;
    Element* registry = nullptr;
    std::uint64_t nextSerial = 1;
    std::uint64_t nextTransaction = 1;
    bool serialExhausted = false;
    bool transactionExhausted = false;
    bool rootActive = true;
    CallbackPhase phase = CallbackPhase::idle;
    std::size_t builds = 0;
    RenderObject viewport;
    bool renderLinksDirty = true;
    ElementHandle focused;
    ElementHandle inputScope;
    Element* scopeRoot() const noexcept;
    bool withinInputScope(const Element&) const noexcept;

    Element* find(ElementHandle) const noexcept;
    void registerElement(Element&) noexcept;
    void unregisterElement(Element&) noexcept;
    bool newTransaction(std::uint64_t&) noexcept;
    UiError mount(WidgetPtr&&, std::size_t depth, std::uint64_t transaction,
                  Owned<Element>& result) noexcept;
    UiError attempt(Element&, std::size_t depth, std::uint64_t transaction,
                    bool staged) noexcept;
    UiError reconcileChild(Element&, WidgetPtr&&, std::size_t depth,
                           std::uint64_t transaction, bool staged) noexcept;
    UiError reconcileChildren(Element&, WidgetList&, std::size_t depth,
                              std::uint64_t transaction, bool staged) noexcept;
    void updateCompatible(Element&, WidgetPtr&&) noexcept;
    void invalidateTransaction(std::uint64_t) noexcept;
    void invalidateSubtree(Element&) noexcept;
    void disposeSubtree(Element&) noexcept;
    void rollback(Owned<Element>&, std::uint64_t) noexcept;
    void commitTransaction(std::uint64_t) noexcept;
    void setTreePhase(Element&, ElementPhase) noexcept;
    bool hasActiveDirty(const Element&) const noexcept;
    UiError pumpTree(Element&, std::size_t depth) noexcept;
    UiError linkRenderTree(Element&, RenderObject&) noexcept;
};

} } // namespace NanoUI::detail
