#pragma once

namespace NanoUI {
namespace detail {

// Terminal cleanup is synchronous on the UI thread. Destruction callbacks must
// not repopulate managers while their owned objects are being released.
inline bool& shutdownInProgress() {
    static bool active = false;
    return active;
}

class ShutdownGuard {
    const bool previous_;

   public:
    ShutdownGuard() : previous_(shutdownInProgress()) { shutdownInProgress() = true; }
    ~ShutdownGuard() { shutdownInProgress() = previous_; }
    ShutdownGuard(const ShutdownGuard&) = delete;
    ShutdownGuard& operator=(const ShutdownGuard&) = delete;
};

}  // namespace detail
}  // namespace NanoUI
