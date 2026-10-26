#include <NanoUI/framework/internal_element.h>

namespace NanoUI { namespace detail {
namespace {
struct ChildPlan {
    ChildPlan() noexcept = default;
    WidgetPtr config;
    Element* match = nullptr;
    Owned<Element> mounted;
    Owned<ChildPlan> next;
};

Owned<Element> detach(Owned<Element>& head, Element* target) noexcept {
    auto* link = &head;
    while (*link && link->get() != target) link = &(*link)->sibling;
    if (!*link) return {};
    Owned<Element> value = std::move(*link);
    *link = std::move(value->sibling);
    return value;
}
} // namespace

UiError BuildOwnerImpl::reconcileChildren(Element& parent, WidgetList& configs,
                                          std::size_t depth, std::uint64_t transaction,
                                          bool staged) noexcept {
    std::size_t count = 0;
    for (auto* node = configs.first(); node; node = node->next.get()) {
        if (!node->widget) return UiError::invalidInput;
        if (++count > options.maxChildren) return UiError::capacityExceeded;
        if (node->widget->key().hasValue()) {
            for (auto* earlier = configs.first(); earlier != node; earlier = earlier->next.get())
                if (earlier->widget->key() == node->widget->key()) return UiError::invalidInput;
        }
    }
    if (!staged && !newTransaction(transaction)) return UiError::capacityExceeded;
    Owned<ChildPlan> plan;
    auto* tail = &plan;
    Element* positional = parent.child.get();
    UiError error = UiError::none;
    for (auto* node = configs.first(); node; node = node->next.get()) {
        auto entry = makeOwned<ChildPlan>(allocator, AllocationKind::container);
        if (!entry) { error = UiError::outOfMemory; break; }
        entry->config = std::move(node->widget);
        if (!staged) {
            if (entry->config->key().hasValue()) {
                for (auto* old = parent.child.get(); old; old = old->sibling.get()) {
                    if (old->widget->key() == entry->config->key() &&
                        old->widget->type() == entry->config->type()) {
                        entry->match = old; break;
                    }
                }
            } else if (positional && !positional->widget->key().hasValue() &&
                       positional->widget->type() == entry->config->type()) {
                entry->match = positional;
            }
        }
        if (positional) positional = positional->sibling.get();
        ChildPlan* current = entry.get();
        *tail = std::move(entry);
        tail = &current->next;
        if (!current->match) {
            error = mount(std::move(current->config), depth, transaction, current->mounted);
            if (error != UiError::none) break;
        }
    }
    if (error != UiError::none) {
        // Invalidate every staged handle before any user's dispose callback.
        invalidateTransaction(transaction);
        for (auto* entry = plan.get(); entry; entry = entry->next.get())
            if (entry->mounted) {
                invalidateSubtree(*entry->mounted);
                disposeSubtree(*entry->mounted);
                entry->mounted.reset();
            }
        return error; // Old live child order/configuration remains untouched.
    }

    if (!staged) commitTransaction(transaction);
    Owned<Element> children;
    auto* childTail = &children;
    for (auto* entry = plan.get(); entry; entry = entry->next.get()) {
        Owned<Element> next = entry->match ? detach(parent.child, entry->match)
                                           : std::move(entry->mounted);
        if (entry->match) updateCompatible(*next, std::move(entry->config));
        if (!staged) setTreePhase(*next, rootActive ? ElementPhase::active : ElementPhase::inactive);
        Element* current = next.get();
        *childTail = std::move(next);
        childTail = &current->sibling;
    }
    for (auto* old = parent.child.get(); old; old = old->sibling.get()) invalidateSubtree(*old);
    while (parent.child) {
        auto old = std::move(parent.child);
        parent.child = std::move(old->sibling);
        disposeSubtree(*old);
    }
    parent.child = std::move(children);
    return UiError::none;
}

} } // namespace NanoUI::detail
