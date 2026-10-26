#pragma once
#include <NanoUI/design/theme.h>
#include <NanoUI/widgets/input_region.h>

namespace NanoUI {
// Keep this model in page State or another owner that outlives the subtree.
// Contents are a builder, not business data or an implicit global theme lookup.
struct TilePresentation {
    ChildBuilder contents;
    Theme theme;
    bool selected = false;
    float height = 0; // Zero selects theme.rowHeight; positive is an explicit local override.
    ActionCallback action = nullptr;
    void* actionPayload = nullptr;
    bool enabled = true;
    FocusCallback focusChanged = nullptr;
};
class ListTile final : public StatelessWidget {
public:
    ListTile(TilePresentation& presentation, Key key = {}) noexcept
        : StatelessWidget(typeId<ListTile>(), key), presentation_(&presentation) {}
    BuildResult build(BuildContext& c) const noexcept override {
        return wrap(makeOwned<SizedBox>(c.allocator(), AllocationKind::widget,
            Size(0, presentation_->height == 0 ? presentation_->theme.rowHeight : presentation_->height),
            ChildBuilder(decoration, presentation_)));
    }
    // Width is filled by a stretch Column's tight cross-axis constraints.
private:
    static BuildResult wrap(WidgetPtr child) noexcept {
        const auto error = child ? UiError::none : UiError::outOfMemory;
        return BuildResult(error, std::move(child));
    }
    static BuildResult decoration(BuildContext& c, void* payload) noexcept {
        auto& p = *static_cast<TilePresentation*>(payload);
        const auto fill = p.selected ? p.theme.selectedBackground : p.theme.background;
        return wrap(makeOwned<DecoratedBox>(c.allocator(), AllocationKind::widget,
            BoxStyle(fill), ChildBuilder(padding, payload)));
    }
    static BuildResult padding(BuildContext& c, void* payload) noexcept {
        auto& p = *static_cast<TilePresentation*>(payload);
        return wrap(makeOwned<Padding>(c.allocator(), AllocationKind::widget,
            Insets(p.theme.outerInset, 0, p.theme.outerInset, 0), ChildBuilder(alignment, payload)));
    }
    static BuildResult alignment(BuildContext& c, void* payload) noexcept {
        auto& p = *static_cast<TilePresentation*>(payload);
        return wrap(makeOwned<Align>(c.allocator(), AllocationKind::widget, p.contents, 0.0f, 0.5f));
    }
    TilePresentation* presentation_;
};
class Button final : public StatelessWidget {
public:
    Button(TilePresentation& presentation, Key key = {}) noexcept
        : StatelessWidget(typeId<Button>(), key), presentation_(&presentation) {}
    BuildResult build(BuildContext& c) const noexcept override {
        auto child = makeOwned<InputRegion>(c.allocator(), AllocationKind::widget,
            presentation_->action, presentation_->actionPayload, ChildBuilder(tile, presentation_), key(),
            presentation_->enabled, presentation_->focusChanged);
        const auto error = child ? UiError::none : UiError::outOfMemory;
        return BuildResult(error, std::move(child));
    }
private:
    static BuildResult tile(BuildContext& c, void* payload) noexcept {
        auto child = makeOwned<ListTile>(c.allocator(), AllocationKind::widget,
            *static_cast<TilePresentation*>(payload));
        const auto error = child ? UiError::none : UiError::outOfMemory;
        return BuildResult(error, std::move(child));
    }
    TilePresentation* presentation_;
};
} // namespace NanoUI
