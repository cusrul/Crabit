#pragma once
#include <stdint.h>

namespace IdleSleep {
// The existing deep-sleep duration is retained on disk as an enable switch.
// The screen timeout is now the sole inactivity deadline; busy guards decide
// whether to enter STOP2 or remain screen-off without tearing down a page.
inline bool due(bool screenOnly, uint32_t screenTimeout, uint32_t deepSetting, uint32_t idle) {
    return !screenOnly && screenTimeout != 0 && deepSetting != 0 && idle >= screenTimeout;
}
class WakeInputGuard {
    bool pending = false;
    uint32_t since = 0;

   public:
    void arm(uint32_t now) {
        since = now;
        pending = true;
    }
    bool consume(uint32_t now, bool pressed) {
        if (!pending)
            return false;
        if (!pressed && uint32_t(now - since) >= 200u)
            pending = false;
        return true;  // Also consume the first released sample.
    }
};
}  // namespace IdleSleep
