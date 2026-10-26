#pragma once
#include "settings_hub.h"

namespace CrabitUI {
namespace detail { class SettingsHubRouteState; }

// Route/session lifetime is managed by the framework's Element/State, not an
// externally prepared model bound to a not-yet-created Navigator owner.
// Font and callback payload still belong to the host and outlive this route.
class SettingsHubRoute final : public NanoUI::StatefulWidget {
public:
    SettingsHubRoute(const NanoUI::Font& font, float height,
                     SettingsAction action, void* payload, NanoUI::Key key = {}) noexcept
        : StatefulWidget(NanoUI::typeId<SettingsHubRoute>(), key), font_(font),
          height_(height), action_(action), payload_(payload) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator&) const noexcept override;
private:
    friend class detail::SettingsHubRouteState;
    const NanoUI::Font& font_;
    float height_;
    SettingsAction action_;
    void* payload_;
};

namespace detail {
class SettingsHubRouteState final : public NanoUI::State {
public:
    SettingsHubRouteState() noexcept = default;
    void initState(NanoUI::BuildContext& c) noexcept override {
        const auto& config = static_cast<const SettingsHubRoute&>(c.widget());
        font_ = &config.font_;
        height_ = config.height_;
        action_ = config.action_;
        payload_ = config.payload_;
        model_ = NanoUI::makeOwned<SettingsHubModel>(c.allocator(), NanoUI::AllocationKind::state,
            c.owner(), config.font_, config.height_, config.action_, config.payload_);
    }
    NanoUI::BuildResult build(NanoUI::BuildContext& c) noexcept override {
        if (!model_) return NanoUI::BuildResult(NanoUI::UiError::outOfMemory, {});
        const auto& config = static_cast<const SettingsHubRoute&>(c.widget());
        // A retained session cannot silently rebind its owner, resources or
        // geometry. Push a new route or supply a new Key for different inputs.
        if (font_ != &config.font_ || height_ != config.height_ ||
            action_ != config.action_ || payload_ != config.payload_)
            return NanoUI::BuildResult(NanoUI::UiError::invalidInput, {});
        auto page = NanoUI::makeOwned<SettingsHub>(c.allocator(), NanoUI::AllocationKind::widget, *model_);
        const auto error = page ? NanoUI::UiError::none : NanoUI::UiError::outOfMemory;
        return NanoUI::BuildResult(error, std::move(page));
    }
    void dispose() noexcept override {
        // Element disposes the borrowing child tree before State::dispose().
        model_.reset();
    }
private:
    NanoUI::Owned<SettingsHubModel> model_;
    const NanoUI::Font* font_ = nullptr;
    float height_ = 0;
    SettingsAction action_ = nullptr;
    void* payload_ = nullptr;
};
} // namespace detail

inline NanoUI::Owned<NanoUI::State> SettingsHubRoute::createState(NanoUI::Allocator& a) const noexcept {
    return NanoUI::makeOwned<detail::SettingsHubRouteState>(a, NanoUI::AllocationKind::state);
}
} // namespace CrabitUI
