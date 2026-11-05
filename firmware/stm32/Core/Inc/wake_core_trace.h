#pragma once
#include <stdint.h>

// Raw register snapshots bracket the HAL STOP2 call, not the WFI instruction
// itself. ISR execution is still masked; reads must not clear device flags.
namespace WakeCoreTrace {
struct Snapshot {
    uint32_t icsr = 0, shcsr = 0, primask = 0, systickValue = 0;
    uint32_t pending[3]{}, enabled[3]{};
    uint32_t exti1 = 0, exti2 = 0, power1 = 0, power2 = 0;
};
inline Snapshot before{}, after{};
inline bool valid = false;
}  // namespace WakeCoreTrace
