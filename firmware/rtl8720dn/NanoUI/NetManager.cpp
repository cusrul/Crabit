#include "NetManager.h"

#include "Protocol.h"

volatile bool ble_is_authenticated = false;
volatile bool ble_was_connected = false;

namespace NetManager {

    volatile uint8_t active_conn_id = 0;
    bool auth_reply_pending = false;
    uint32_t auth_reply_tick = 0;
    bool auth_reply_success = false;
    bool test_reply_pending = false;
    String test_reply_payload = "";

    static String target_ssid = "";
    static String target_pwd = "";
    static bool wifi_connect_req = false;
    static bool wifi_disconnect_req = false;

    RadioSvc::Result initBLE(uint16_t request_id) {
        return RadioSvc::requestStart(request_id);
    }

    RadioSvc::Result stopBLE() {
        return RadioSvc::requestStop();
    }

    RadioSvc::Result safe_ble_notify(String payload, uint8_t conn_id) {
        return RadioSvc::enqueueNotify(payload.c_str(), conn_id);
    }

    void sendDiagnosticWifi(uint16_t request_id, uint8_t conn_id) {
        String id = String((unsigned)request_id);
        String value = String("status=") + String((int)WiFi.status());
        if (WiFi.status() == WL_CONNECTED) {
            IPAddress ip = WiFi.localIP();
            value = String("connected,ip=") + String(ip[0]) + "." +
                    String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        }
        safe_ble_notify("{\"t\":\"ack\",\"v\":\"" + id + "\"}", conn_id);
        safe_ble_notify("{\"i\":" + id +
                        ",\"t\":\"diag/wifi\",\"v\":\"" + value + "\"}", conn_id);
    }

    void setWiFi(bool enable, String ssid, String pwd) {
        if (enable) {
            target_ssid = ssid;
            target_pwd = pwd;
            wifi_connect_req = true;
        } else {
            wifi_disconnect_req = true;
        }
    }

    void tickWiFi() {
        if (wifi_connect_req) {
            wifi_connect_req = false;
            Protocol::store_background_msg("sys/notify", "Wi-Fi Conn...");

            int status = WiFi.begin((char*)target_ssid.c_str(), target_pwd.c_str());
            if (status == WL_CONNECTED) {
                IPAddress ip = WiFi.localIP();
                String ip_str = String(ip[0]) + "." + String(ip[1]) + "." +
                                String(ip[2]) + "." + String(ip[3]);
                Protocol::store_background_msg("sys/notify", "WLAN OK: " + ip_str);
                Protocol::send_to_stm32("{\"t\":\"sys/net\",\"v\":\"WIFI_ON\"}");
            } else {
                Protocol::store_background_msg("sys/notify", "WLAN Failed!");
                Protocol::send_to_stm32("{\"t\":\"sys/net\",\"v\":\"WIFI_OFF\"}");
            }
        }

        if (wifi_disconnect_req) {
            wifi_disconnect_req = false;
            WiFi.disconnect();
            Protocol::store_background_msg("sys/notify", "WLAN Disconnected");
            Protocol::send_to_stm32("{\"t\":\"sys/net\",\"v\":\"WIFI_OFF\"}");
        }
    }

    void tickPairing() {
        // Kept for the old main-loop call site. Pairing and BLE SDK work are
        // performed by RadioSvc::service() in Arduino loop; this is its deadline
        // observer and never calls BLE SDK functions.
        RadioSvc::tick();
    }
}
