#pragma once

#include <NanoUI/framework/widget.h>
#include <NanoUI/framework/widget_list.h>

namespace NanoUI {

struct BuildResult {
    BuildResult() noexcept = default;
    BuildResult(UiError result, WidgetPtr value, WidgetList values = {}) noexcept
        : error(result), child(std::move(value)), children(std::move(values)) {}
    UiError error = UiError::none;
    WidgetPtr child{};
    // Single child remains source-compatible. Set either child or children.
    WidgetList children{};
};

} // namespace NanoUI
