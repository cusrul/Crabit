#pragma once

// Product-shaped composition example, not an active firmware entry point.
#include <NanoUI/framework/build_owner.h>
#include <NanoUI/components/list_tile.h>
#include <NanoUI/widgets/text.h>
#include <NanoUI/widgets/scroll_controller.h>

namespace CrabitUI {
enum class SettingsDestination { display, power, gesture, connect, exit };
#if defined(__cpp_noexcept_function_type)
using SettingsAction = bool (*)(SettingsDestination, void*) noexcept;
#else
using SettingsAction = bool (*)(SettingsDestination, void*); // Must not throw.
#endif

class SettingsHub;

// One UI-context owner. Borrowed owner/font/action payload must outlive this
// model; this model must outlive every Element/Widget borrowing its row recipes.
// Non-movable because the recipes point back to their containing model.
class SettingsHubModel final {
public:
    static constexpr std::size_t count = 5;
    SettingsHubModel(NanoUI::BuildOwner& owner, const NanoUI::Font& font,
                     float viewportHeight, SettingsAction action, void* payload) noexcept
        : owner_(owner), font_(font), height_(viewportHeight), action_(action), payload_(payload) {
        for (std::size_t i = 0; i < count; ++i) {
            rows_[i].model = this;
            rows_[i].index = i;
            auto& tile = rows_[i].tile;
            tile.theme.textHeight = 12;
            tile.height = rowHeight;
            tile.contents = NanoUI::ChildBuilder(rowText, &rows_[i]);
            tile.action = action_ ? rowAction : nullptr;
            tile.actionPayload = &rows_[i];
            tile.focusChanged = rowFocus;
        }
    }
    SettingsHubModel(const SettingsHubModel&) = delete;
    SettingsHubModel& operator=(const SettingsHubModel&) = delete;
    static const char* label(std::size_t index) noexcept {
        switch (index) {
        case 0: return "DISPLAY";
        case 1: return "POWER";
        case 2: return "GESTURE";
        case 3: return "CONNECT";
        case 4: return "EXIT";
        default: return "";
        }
    }
    static NanoUI::Key rowKey(std::size_t index) noexcept { return NanoUI::Key::value(index + 1); }
    float viewportHeight() const noexcept { return height_; }
    float scrollOffset() const noexcept { return scroll_.offset(); }
    int selectedIndex() const noexcept { return selected_; }
    NanoUI::UiError error() const noexcept { return error_; }
private:
    friend class SettingsHub;
    enum { headingHeight = 16, rowHeight = 18, rowGap = 2, listExtent = 98 };
    struct Row {
        SettingsHubModel* model = nullptr;
        std::size_t index = 0;
        NanoUI::TilePresentation tile;
    };
    static NanoUI::BuildResult wrap(NanoUI::WidgetPtr child) noexcept {
        const auto error = child ? NanoUI::UiError::none : NanoUI::UiError::outOfMemory;
        return NanoUI::BuildResult(error, std::move(child));
    }
    static NanoUI::BuildResult rowText(NanoUI::BuildContext& c, void* payload) noexcept {
        const auto& row = *static_cast<Row*>(payload);
        return wrap(NanoUI::makeText(c.allocator(), label(row.index), row.model->font_, 12,
            row.tile.selected ? row.tile.theme.selectedForeground : row.tile.theme.foreground));
    }
    static bool rowAction(NanoUI::InputAction input, void* payload) noexcept {
        auto& row = *static_cast<Row*>(payload);
        auto& m = *row.model;
        if (!m.action_) return false;
        if (input == NanoUI::InputAction::back) return m.action_(SettingsDestination::exit, m.payload_);
        if (input != NanoUI::InputAction::activate) return false;
        return m.action_(static_cast<SettingsDestination>(row.index), m.payload_);
    }
    static void rowFocus(bool focused, void* payload) noexcept {
        auto& row = *static_cast<Row*>(payload);
        auto& m = *row.model;
        row.tile.selected = focused;
        if (focused) {
            m.selected_ = static_cast<int>(row.index);
            m.remember(m.scroll_.reveal(static_cast<float>(row.index * (rowHeight + rowGap)),
                static_cast<float>(row.index * (rowHeight + rowGap) + rowHeight),
                m.height_ - headingHeight, listExtent));
        } else if (m.selected_ == static_cast<int>(row.index)) {
            m.selected_ = -1;
        }
        m.remember(m.owner_.requestBuild(m.owner_.root()));
    }
    void remember(NanoUI::UiError error) noexcept {
        if (error != NanoUI::UiError::none && error_ == NanoUI::UiError::none) error_ = error;
    }
    bool valid() const noexcept { return std::isfinite(height_) && height_ >= 34 && height_ <= 32767; }
    NanoUI::BuildOwner& owner_;
    const NanoUI::Font& font_;
    float height_;
    SettingsAction action_;
    void* payload_;
    Row rows_[count];
    NanoUI::ScrollController scroll_;
    int selected_ = -1;
    NanoUI::UiError error_ = NanoUI::UiError::none; // First callback error is observable.
};

// Page -> SizedBox/Decoration -> Linear -> fixed heading + Scroll -> Buttons
// -> ListTile -> Padding/Align/Text. No page-specific RenderObject or platform IO.
class SettingsHub final : public NanoUI::StatelessWidget {
public:
    explicit SettingsHub(SettingsHubModel& model) noexcept
        : StatelessWidget(NanoUI::typeId<SettingsHub>()), model_(model) {}
    NanoUI::BuildResult build(NanoUI::BuildContext& c) const noexcept override {
        if (!model_.valid()) return NanoUI::BuildResult(NanoUI::UiError::invalidInput, {});
        return wrap(NanoUI::makeOwned<NanoUI::SizedBox>(c.allocator(), NanoUI::AllocationKind::widget,
            NanoUI::Size(128, model_.height_), NanoUI::ChildBuilder(surface, &model_)));
    }
private:
    using Model = SettingsHubModel;
    static NanoUI::BuildResult wrap(NanoUI::WidgetPtr child) noexcept { return Model::wrap(std::move(child)); }
    static NanoUI::BuildResult surface(NanoUI::BuildContext& c, void* payload) noexcept {
        return wrap(NanoUI::makeOwned<NanoUI::DecoratedBox>(c.allocator(), NanoUI::AllocationKind::widget,
            NanoUI::BoxStyle(0x000000FFu), NanoUI::ChildBuilder(column, payload)));
    }
    static NanoUI::BuildResult column(NanoUI::BuildContext& c, void* payload) noexcept {
        return wrap(NanoUI::makeOwned<NanoUI::Linear>(c.allocator(), NanoUI::AllocationKind::widget,
            NanoUI::ChildBuilder(parts, payload), NanoUI::Axis::vertical, 0, NanoUI::CrossAlign::stretch));
    }
    static NanoUI::BuildResult parts(NanoUI::BuildContext& c, void* payload) noexcept {
        auto& m = *static_cast<Model*>(payload);
        NanoUI::BuildResult result;
        NanoUI::WidgetPtr title = NanoUI::makeOwned<NanoUI::SizedBox>(c.allocator(), NanoUI::AllocationKind::widget,
            NanoUI::Size(128, Model::headingHeight), NanoUI::ChildBuilder(titleAlign, payload));
        if (!title) return NanoUI::BuildResult(NanoUI::UiError::outOfMemory, {});
        result.error = result.children.append(c.allocator(), title);
        if (result.error != NanoUI::UiError::none) return result;
        NanoUI::WidgetPtr list = NanoUI::makeOwned<NanoUI::Scroll>(c.allocator(), NanoUI::AllocationKind::widget,
            m.scrollOffset(), m.height_ - Model::headingHeight, NanoUI::ChildBuilder(listColumn, payload));
        if (!list) { result.error = NanoUI::UiError::outOfMemory; return result; }
        result.error = result.children.append(c.allocator(), list);
        return result;
    }
    static NanoUI::BuildResult titleAlign(NanoUI::BuildContext& c, void* payload) noexcept {
        return wrap(NanoUI::makeOwned<NanoUI::Align>(c.allocator(), NanoUI::AllocationKind::widget,
            NanoUI::ChildBuilder(titleText, payload)));
    }
    static NanoUI::BuildResult titleText(NanoUI::BuildContext& c, void* payload) noexcept {
        auto& m = *static_cast<Model*>(payload);
        return wrap(NanoUI::makeText(c.allocator(), "SETTINGS", m.font_, 12, 0xFFFFFFFFu));
    }
    static NanoUI::BuildResult listColumn(NanoUI::BuildContext& c, void* payload) noexcept {
        return wrap(NanoUI::makeOwned<NanoUI::Linear>(c.allocator(), NanoUI::AllocationKind::widget,
            NanoUI::ChildBuilder(buttons, payload), NanoUI::Axis::vertical, Model::rowGap, NanoUI::CrossAlign::stretch));
    }
    static NanoUI::BuildResult buttons(NanoUI::BuildContext& c, void* payload) noexcept {
        auto& m = *static_cast<Model*>(payload);
        NanoUI::BuildResult result;
        for (std::size_t i = 0; i < Model::count; ++i) {
            NanoUI::WidgetPtr button = NanoUI::makeOwned<NanoUI::Button>(c.allocator(), NanoUI::AllocationKind::widget,
                m.rows_[i].tile, Model::rowKey(i));
            if (!button) { result.error = NanoUI::UiError::outOfMemory; return result; }
            result.error = result.children.append(c.allocator(), button);
            if (result.error != NanoUI::UiError::none) return result;
        }
        return result;
    }
    Model& model_;
};
} // namespace CrabitUI
