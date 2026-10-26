#ifndef CRABIT_APP_BLE_ADVERT_TEST_H
#define CRABIT_APP_BLE_ADVERT_TEST_H

#include "../src/NanoUI.h"
#include "../RadioSvc.h"
#include <BLEDevice.h>
#include <BLEAdvert.h>
#include <BLEAdvertData.h>

// Foreground Arduino-owner experiment, not the external payload-rotation library.
// No pairing, random-address rotation, remote trigger or persistent settings.
class AppBleAdvertTest : public Application {
public:
    const char* getAppName() override { return "BLE ADV TEST"; }
    AppMode getMode() override { return APP_MODE_EXCLUSIVE; }

    void runExclusive(Canvas* canvas, InputDriver* input) override {
        const auto owner = RadioSvc::snapshot();
        // initialized means RadioSvc queues/owner context were set up in
        // AmebaServices::init(); it does not mean the radio is currently owned.
        const bool busy = owner.state != RadioSvc::IDLE ||
                          RadioSvc::companionActive();
        bool initialized = false;
        bool advertising = false;
        uint32_t started = 0;
        uint8_t interval = 1;
        const uint16_t intervalUnits[] = {500, 1000, 2000}; // SDK takes milliseconds.
        const char* intervalLabels[] = {"500 ms", "1000 ms", "2000 ms"};

        while (is_display_active) {
            const UIEvent event = input->poll();
            if (event.type == EVT_BACK) break;
            if (!busy && !advertising) {
                if (event.type == EVT_UP) interval = (interval + 2) % 3;
                if (event.type == EVT_DOWN) interval = (interval + 1) % 3;
            }
            if (!busy && event.type == EVT_OK) {
                if (advertising) {
                    BLE.configAdvert()->stopAdv();
                    advertising = false;
                } else {
                    if (!initialized) {
                        canvas->clear();
                        canvas->drawText(2, 18, "BLE STARTING...", 1);
                        canvas->display();
                        BLE.init();
                        BLE.configAdvert()->setAdvType(GAP_ADTYPE_ADV_NONCONN_IND);
                        BLEAdvertData data;
                        data.addFlags(GAP_ADTYPE_FLAGS_GENERAL | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED);
                        data.addCompleteName("Crabit-Test");
                        BLE.configAdvert()->setAdvData(data);
                        BLE.configAdvert()->setMinInterval(intervalUnits[interval]);
                        BLE.configAdvert()->setMaxInterval(intervalUnits[interval]);
                        initialized = true;
                        BLE.beginPeripheral();
                    } else {
                        BLE.configAdvert()->setMinInterval(intervalUnits[interval]);
                        BLE.configAdvert()->setMaxInterval(intervalUnits[interval]);
                        BLE.configAdvert()->updateAdvertParams();
                        BLE.configAdvert()->startAdv();
                    }
                    advertising = true;
                    started = hal_get_ticks();
                }
            }
            if (advertising && uint32_t(hal_get_ticks() - started) >= 60000) {
                BLE.configAdvert()->stopAdv();
                advertising = false;
            }
            canvas->clear();
            canvas->drawText(2, 2, "BLE ADV TEST", 1);
            canvas->drawText(2, 16, busy ? "STOP PHONE BLE FIRST" : "Crabit-Test", 1);
            canvas->drawText(2, 28, busy ? "BUSY" : (advertising ? "ADV REQUEST / 60s" : (initialized ? "STOP REQUESTED" : "READY")), 1);
            canvas->drawText(2, 40, intervalLabels[interval], 1);
            canvas->drawText(2, 54, busy ? "BACK: EXIT" : "OK: START/STOP  BACK", 1);
            canvas->display();
            safe_delay(20);
        }
        if (initialized) {
            BLE.configAdvert()->stopAdv();
            BLE.deinit();
        }
    }
};
#endif
