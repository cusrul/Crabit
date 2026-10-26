#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>

#include "RadioSvc.h"

extern volatile bool ble_is_authenticated;
extern volatile bool ble_was_connected;

namespace NetManager {
    extern volatile uint8_t active_conn_id;
    extern bool auth_reply_pending;
    extern uint32_t auth_reply_tick;
    extern bool auth_reply_success;

    extern bool test_reply_pending;
    extern String test_reply_payload;

    // Compatibility facade: the BLE SDK is owned by RadioSvc.
    RadioSvc::Result initBLE(uint16_t request_id = 0);
    RadioSvc::Result stopBLE();
    RadioSvc::Result safe_ble_notify(String payload, uint8_t conn_id);

    void setWiFi(bool enable, String ssid = "", String pwd = "");
    void tickWiFi();
    void tickPairing();
    void sendDiagnosticWifi(uint16_t request_id, uint8_t conn_id);
}

#endif
