#pragma once

#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/widget.h>

namespace NanoUI {

#if defined(__cpp_noexcept_function_type)
using InputCallback = void (*)(void*) noexcept;
#else
using InputCallback = void (*)(void*); // Caller must not throw.
#endif

enum class NavigationStatus { queued, complete, atRoot, busy, failed };
struct NavigationResult {
    NavigationResult(NavigationStatus statusValue = NavigationStatus::complete,
                     UiError errorValue = UiError::none,
                     std::size_t appliedValue = 0) noexcept
        : status(statusValue), error(errorValue), applied(appliedValue) {}
    NavigationStatus status = NavigationStatus::complete;
    UiError error = UiError::none;
    std::size_t applied = 0;
};
struct NavigatorOptions {
    NavigatorOptions(std::size_t routes = 16, std::size_t pending = 16,
                     OwnerOptions ownerValue = {}) noexcept
        : maxRoutes(routes), maxPending(pending), owner(ownerValue) {}
    std::size_t maxRoutes = 16;
    std::size_t maxPending = 16;
    OwnerOptions owner{};
};

// One UI-context owner. Requests only enqueue; the host invokes commit after
// input callbacks finish. Never commit from a build/layout/paint callback.
class Navigator final {
public:
    explicit Navigator(Allocator&, NavigatorOptions = {}) noexcept;
    ~Navigator() noexcept;
    Navigator(const Navigator&) = delete;
    Navigator& operator=(const Navigator&) = delete;
    NavigationResult push(WidgetPtr&) noexcept; // Failure retains configuration.
    NavigationResult pop() noexcept;
    NavigationResult commit() noexcept;
    // Protect input callback traversal: enqueue is allowed, commit is not.
    UiError dispatch(InputCallback callback, void* payload) noexcept;
    UiError dispatchInput(InputAction) noexcept; // Current route only; never bubbles to hidden routes.
    UiError cancelPending() noexcept;
    BuildOwner* current() const noexcept;
    std::size_t depth() const noexcept { return routes_; }
    std::size_t pending() const noexcept { return pending_; }
    UiError saveFocus(ElementHandle) noexcept;
    ElementHandle focus() const noexcept;
private:
    struct Route {
        Route(Allocator& allocator, OwnerOptions options) noexcept : owner(allocator, options) {}
        BuildOwner owner;
        ElementHandle focus{};
        Owned<Route> previous;
    };
    struct Request {
        explicit Request(bool pushValue) noexcept : pushing(pushValue) {}
        bool pushing;
        WidgetPtr widget;
        Owned<Request> next;
    };
    Allocator& allocator_;
    NavigatorOptions options_;
    Owned<Route> top_;
    Owned<Request> queue_;
    std::size_t routes_ = 0;
    std::size_t pending_ = 0;
    bool committing_ = false;
    bool dispatching_ = false;
};

} // namespace NanoUI
