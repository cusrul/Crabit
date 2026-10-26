#pragma once
#include "list_tile.h"

namespace NanoUI {
struct DialogPresentation {
    ChildBuilder body;
    Theme theme;
    float margin = 5;
};
// Layout/presentation only. A route/focus controller owns modal interaction.
class Dialog final : public StatelessWidget {
public:
    Dialog(DialogPresentation& presentation, Key key = {}) noexcept
        : StatelessWidget(typeId<Dialog>(), key), presentation_(&presentation) {}
    BuildResult build(BuildContext& c) const noexcept override {
        return wrap(makeOwned<Padding>(c.allocator(), AllocationKind::widget,
            Insets(presentation_->margin), ChildBuilder(align, presentation_)));
    }
private:
    static BuildResult wrap(WidgetPtr child) noexcept {
        const auto error = child ? UiError::none : UiError::outOfMemory;
        return BuildResult(error, std::move(child));
    }
    static BuildResult align(BuildContext& c, void* payload) noexcept {
        return wrap(makeOwned<Align>(c.allocator(), AllocationKind::widget, ChildBuilder(box, payload)));
    }
    static BuildResult box(BuildContext& c, void* payload) noexcept {
        auto& p = *static_cast<DialogPresentation*>(payload);
        return wrap(makeOwned<DecoratedBox>(c.allocator(), AllocationKind::widget,
            BoxStyle(p.theme.background, p.theme.foreground, 1), ChildBuilder(contents, payload)));
    }
    static BuildResult contents(BuildContext& c, void* payload) noexcept {
        auto& p = *static_cast<DialogPresentation*>(payload);
        return wrap(makeOwned<Padding>(c.allocator(), AllocationKind::widget, Insets(3), p.body));
    }
    DialogPresentation* presentation_;
};
} // namespace NanoUI
