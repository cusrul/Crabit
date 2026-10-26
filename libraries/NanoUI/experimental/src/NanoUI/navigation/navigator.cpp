#include <NanoUI/navigation/navigator.h>

namespace NanoUI {
namespace {
NavigationResult failure(UiError error) noexcept {
    return {NavigationStatus::failed, error, 0};
}
struct CommitGuard {
    explicit CommitGuard(bool& active) noexcept : value(active) { value = true; }
    ~CommitGuard() noexcept { value = false; }
    bool& value;
};
}
Navigator::Navigator(Allocator& allocator, NavigatorOptions options) noexcept
    : allocator_(allocator), options_(options) {}
Navigator::~Navigator() noexcept {
    CommitGuard guard(committing_);
    queue_.reset();
    while (top_) {
        auto route = std::move(top_);
        top_ = std::move(route->previous);
    }
}

NavigationResult Navigator::push(WidgetPtr& widget) noexcept {
    if (committing_) return {NavigationStatus::busy, UiError::invalidPhase, 0};
    if (!widget || options_.maxRoutes == 0) return failure(UiError::invalidInput);
    if (pending_ >= options_.maxPending) return failure(UiError::capacityExceeded);
    auto request = makeOwned<Request>(allocator_, AllocationKind::container, true);
    if (!request) return failure(UiError::outOfMemory);
    request->widget = std::move(widget);
    auto* tail = &queue_;
    while (*tail) tail = &(*tail)->next;
    *tail = std::move(request);
    ++pending_;
    return {NavigationStatus::queued, UiError::none, 0};
}
NavigationResult Navigator::pop() noexcept {
    if (committing_) return {NavigationStatus::busy, UiError::invalidPhase, 0};
    if (pending_ >= options_.maxPending) return failure(UiError::capacityExceeded);
    auto request = makeOwned<Request>(allocator_, AllocationKind::container, false);
    if (!request) return failure(UiError::outOfMemory);
    auto* tail = &queue_;
    while (*tail) tail = &(*tail)->next;
    *tail = std::move(request);
    ++pending_;
    return {NavigationStatus::queued, UiError::none, 0};
}
NavigationResult Navigator::commit() noexcept {
    if (committing_ || dispatching_ || (top_ && !top_->owner.atSafePoint()))
        return {NavigationStatus::busy, UiError::invalidPhase, 0};
    CommitGuard guard(committing_);
    NavigationResult result;
    while (queue_) {
        auto request = std::move(queue_);
        queue_ = std::move(request->next);
        --pending_;
        if (request->pushing) {
            if (routes_ >= options_.maxRoutes) {
                result.status = NavigationStatus::failed;
                result.error = UiError::capacityExceeded;
                return result;
            }
            auto route = makeOwned<Route>(allocator_, AllocationKind::container,
                                          allocator_, options_.owner);
            if (!route) {
                result.status = NavigationStatus::failed;
                result.error = UiError::outOfMemory;
                return result;
            }
            const auto mounted = route->owner.setRoot(std::move(request->widget));
            if (mounted != UiError::none) {
                result.status = NavigationStatus::failed; result.error = mounted;
                return result;
            }
            if (top_) top_->owner.setRootActive(false);
            route->previous = std::move(top_);
            top_ = std::move(route);
            ++routes_;
        } else {
            if (routes_ <= 1) {
                result.status = NavigationStatus::atRoot;
                return result;
            }
            auto old = std::move(top_);
            top_ = std::move(old->previous);
            --routes_;
            // Old tree is unmounted before resuming the revealed route.
            old.reset();
            top_->owner.setRootActive(true);
        }
        ++result.applied;
    }
    return result;
}
UiError Navigator::dispatch(InputCallback callback, void* payload) noexcept {
    if (committing_ || dispatching_) return UiError::invalidPhase;
    if (!callback) return UiError::invalidInput;
    CommitGuard guard(dispatching_);
    callback(payload);
    return UiError::none;
}
UiError Navigator::cancelPending() noexcept {
    if (committing_ || dispatching_ || (top_ && !top_->owner.atSafePoint())) return UiError::invalidPhase;
    CommitGuard guard(committing_);
    queue_.reset(); pending_ = 0;
    return UiError::none;
}
UiError Navigator::dispatchInput(InputAction action) noexcept {
    if (committing_ || dispatching_) return UiError::invalidPhase;
    return top_ ? top_->owner.dispatchFocused(action) : UiError::invalidInput;
}
BuildOwner* Navigator::current() const noexcept { return top_ ? &top_->owner : nullptr; }
UiError Navigator::saveFocus(ElementHandle handle) noexcept {
    if (committing_) return UiError::invalidPhase;
    if (!top_) return UiError::staleHandle;
    const auto snapshot = top_->owner.inspect(handle);
    if (snapshot.error != UiError::none) return snapshot.error;
    top_->focus = handle;
    return UiError::none;
}
ElementHandle Navigator::focus() const noexcept {
    return top_ && top_->owner.inspect(top_->focus).error == UiError::none ? top_->focus
                                                                       : ElementHandle{};
}
} // namespace NanoUI
