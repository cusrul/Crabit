#ifndef CRABIT_BLE_CONTROLS_H
#define CRABIT_BLE_CONTROLS_H

#include "NanoUI/NanoUI.h"
#include "companion_ble.h"

namespace Crabit {
// UI task only. Both physical shortcut and Settings use the proven owner.
inline void toggleBleFromUi() {
    const bool was_active = CompanionBle::active();
    if (CompanionBle::transitioning() || !CompanionBle::toggle()) {
        Popup::showToast("BLE BUSY");
    } else {
        Popup::showToast(was_active ? "BLE STOPPING" : "BLE STARTING");
    }
}
}  // namespace Crabit
#endif
