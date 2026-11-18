#pragma once

#include <stdint.h>
#include "ameba_session.h"
#include "axp2101.h"
#include "companion_ble.h"

extern volatile bool g_coproc_active;

// Read-only BW16 power-off proof for the Settings page. Callers run on the UI
// task, which also holds the I2C bus lock while dispatching Settings input, so
// this register read never races the UI-task boot helper that writes DCDC3.
// I2C failure is reported as unreadable and must be rejected by the caller.
namespace Crabit {
namespace Bw16Power {

enum class Availability : uint8_t { Off, Running, Unreadable };

inline Availability availability() {
    if (g_coproc_active || Crabit::amebaSessionActive() ||
        Crabit::CompanionBle::active() || Crabit::CompanionBle::transitioning())
        return Availability::Running;
    uint8_t control = 0;
    if (AXP2101_ReadReg(AXP2101_DC_ONOFF_DVM_CTRL, &control) != HAL_OK)
        return Availability::Unreadable;
    // Bit 2 of register 0x80 is the BW16 DCDC3 enable controlled by the boot helper.
    return (control & 0x04u) ? Availability::Running : Availability::Off;
}

}  // namespace Bw16Power
}  // namespace Crabit
