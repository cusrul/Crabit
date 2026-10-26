#include <NanoUI/framework/internal_element.h>

namespace NanoUI {
FrameResult BuildOwner::frame(PaintCanvas& canvas, Size size) noexcept {
    FrameResult result;
    if (!std::isfinite(size.width) || !std::isfinite(size.height) ||
        size.width <= 0 || size.height <= 0) {
        result.error = UiError::invalidInput;
        return result;
    }
    const auto built = pump();
    result.error = built.error;
    result.builds = built.builds;
    result.needsFrame = built.needsFrame;
    if (result.error != UiError::none || built.needsFrame || !impl_) return result;
    auto& owner = *impl_;
    if (!owner.root || !owner.rootActive) return result;
    detail::OperationPhaseGuard operation(owner.phase, detail::CallbackPhase::frame);
    // Only registry members are live; inactive/staged Elements are never painted.
    if (owner.renderLinksDirty) {
        owner.viewport.clearBorrowedChildren();
        for (auto* element = owner.registry; element; element = element->registryNext)
            if (element->render) element->render->clearBorrowedChildren();
        result.error = owner.linkRenderTree(*owner.root, owner.viewport);
        if (result.error != UiError::none) return result;
        owner.renderLinksDirty = false;
    }
    if (!owner.viewport.needsLayout() && !owner.viewport.needsPaint() &&
        owner.viewport.constraints() == BoxConstraints::tight(size)) return result;
    owner.viewport.layout(BoxConstraints::tight(size));
    if (owner.viewport.needsLayout()) { result.needsFrame = true; return result; }
    owner.viewport.paint(canvas);
    result.painted = true;
    result.needsFrame = owner.viewport.needsLayout() || owner.viewport.needsPaint();
    return result;
}
} // namespace NanoUI
