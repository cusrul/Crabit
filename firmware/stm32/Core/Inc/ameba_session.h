#ifndef CRABIT_AMEBA_SESSION_H
#define CRABIT_AMEBA_SESSION_H

#include <atomic>
#include <stdint.h>

// STM32 product scheduling state, not a NanoUI or display-ownership API.
// g_coproc_active separately records the existing physical OLED handover.
namespace Crabit {
enum class AmebaPhase : uint8_t { Inactive, Transition, Running };
extern std::atomic<AmebaPhase> ameba_phase;

inline void setAmebaPhase(AmebaPhase phase) {
    ameba_phase.store(phase, std::memory_order_release);
}
inline bool amebaSessionActive() {
    return ameba_phase.load(std::memory_order_acquire) != AmebaPhase::Inactive;
}
inline bool amebaTransitionActive() {
    return ameba_phase.load(std::memory_order_acquire) == AmebaPhase::Transition;
}
} // namespace Crabit

// UI-task-only, after the normal BW16 boot wait and before OLED handover.
// Restores RX only; does not initialize UART/TX, boot pins or the power domain.
extern "C" bool STM32_PrepareAmebaUartRx(void);

#endif
