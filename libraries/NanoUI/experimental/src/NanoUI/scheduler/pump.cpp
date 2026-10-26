#include <NanoUI/framework/internal_element.h>

namespace NanoUI { namespace detail {

UiError BuildOwnerImpl::pumpTree(Element& element, std::size_t depth) noexcept {
    if (element.phase != ElementPhase::active) return UiError::none;
    if (depth > options.maxDepth) return UiError::capacityExceeded;
    if (element.dirty) {
        if (builds >= options.maxBuildsPerOperation) return UiError::none;
        const UiError result = attempt(element, depth, 0, false);
        if (result != UiError::none) return result;
    }
    for (auto* child = element.child.get(); child; child = child->sibling.get()) {
        const UiError result = pumpTree(*child, depth + 1);
        if (result != UiError::none) return result;
    }
    return UiError::none;
}

} } // namespace NanoUI::detail
