#include <NanoUI/framework/build_context.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/internal_element.h>

namespace NanoUI {

Widget::~Widget() noexcept = default;
State::~State() noexcept = default;
void State::initState(BuildContext&) noexcept {}
void State::didUpdateWidget(const Widget&, BuildContext&) noexcept {}
void State::dispose() noexcept {}
BuildResult RenderWidget::build(BuildContext&) const noexcept { return {}; }

BuildContext::BuildContext(const Widget& widget, ElementHandle handle,
                           Allocator& allocator, BuildOwner& owner) noexcept
    : widget_(&widget), handle_(handle), allocator_(&allocator), owner_(&owner) {}
const Widget& BuildContext::widget() const noexcept { return *widget_; }
ElementHandle BuildContext::handle() const noexcept { return handle_; }
Allocator& BuildContext::allocator() const noexcept { return *allocator_; }
BuildOwner& BuildContext::owner() const noexcept { return *owner_; }

} // namespace NanoUI

namespace NanoUI { namespace detail {

Element::Element(BuildOwnerImpl& ownerValue, std::uint64_t serialValue,
                 WidgetPtr&& widgetValue, std::uint64_t transactionValue) noexcept
    : owner(&ownerValue), serial(serialValue), transaction(transactionValue),
      widget(std::move(widgetValue)) {}

BuildResult Element::invokeBuild() noexcept {
    // Direct construction also works on the SDK compiler without guaranteed
    // C++17 copy elision. Context remains non-copyable and callback-scoped.
    BuildContext callbackContext(*widget, ElementHandle{&owner->publicOwner, serial},
                                 owner->allocator, owner->publicOwner);
    switch (widget->kind()) {
    case WidgetKind::leaf:
        return BuildResult{};
    case WidgetKind::stateless:
        return static_cast<const StatelessWidget&>(*widget).build(callbackContext);
    case WidgetKind::stateful:
        return state->build(callbackContext);
    case WidgetKind::rendering:
        return static_cast<const RenderWidget&>(*widget).build(callbackContext);
    }
    return BuildResult{UiError::invalidInput, {}};
}

void Element::updateWidget(WidgetPtr&& replacement) noexcept {
    widget = std::move(replacement);
}

void Element::setPhase(ElementPhase newPhase) noexcept {
    phase = newPhase;
}

} } // namespace NanoUI::detail
