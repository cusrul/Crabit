#ifndef CRABIT_APP_BLE_LINK_H
#define CRABIT_APP_BLE_LINK_H

#include "NanoUI/NanoUI.h"
#include "companion_ble.h"
#include "components/action_button.h"

// A view of the platform session, not its owner: Back never powers off BLE.
class BleLinkApp : public Application {
    NanoUI::NanoUIText status_line = "OFF", action_line = "START";
    NanoUI::NanoUIText pair_line = "DISABLED";
public:
    const char* getAppName() override { return "BLE_LINK"; }
    void onTick() override {
        status_line = Crabit::CompanionBle::status();
        action_line = Crabit::CompanionBle::active() ? "STOP" : "START";
        pair_line = Crabit::CompanionBle::pairTransportStatus();
        if (WindowManager::instance) WindowManager::instance->markDirty();
    }
    Widget* buildUI() override {
        return new Container(new Column({
            new Text(Crabit::CompanionBle::uartProbeOnly() ? "UART PING" : "BLE LINK", 8),
            new SlidingText(&status_line, 8),
            new Text("Clock: UP+DOWN 1s", 8),
            new Row({
                new Button(new Container(new SlidingText(&action_line, 8), 48, 14), []() {
                    if (!Crabit::CompanionBle::toggle()) Popup::showToast("BLE BUSY");
                }),
                new ActionButton("BACK", []() { AppManager::exitCurrentApp(); })
            }),
            new SlidingText(&pair_line, 8)
        }), 128, 64);
    }
};
#endif
