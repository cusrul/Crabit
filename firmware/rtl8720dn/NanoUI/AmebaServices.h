#ifndef AMEBA_SERVICES_H
#define AMEBA_SERVICES_H

#include "AmebaEventBus.h"
#include "AmebaStore.h"
#include "NetManager.h"
#include "Protocol.h"
#include "AmebaTypes.h" // 提供 BW16Mode 状态枚举
#include "src/NanoUI.h"
#include "src/CrabitBuildProfile.h"
#include "NetworkSync.h"
#include <queue.h>

// 告诉编译器这些变量在 main 里面
extern volatile BW16Mode current_mode;
extern volatile bool is_display_active;
extern void releaseOledBus();
extern void start_ui_mode();

class AmebaServices {
private:
    struct PendingCommand {
        uint8_t kind;
        uint16_t request_id;
        char value[96];
    };
    struct PendingNotification {
        uint16_t request_id;
        char topic[CrabitWire::MAX_TOPIC + 1];
        char value[CrabitWire::MAX_VALUE + 1];
    };
    struct PendingSensors {
        int imu[3];
        int pat[3];
        int battery;
        int buttons;
    };
    struct PendingWifiDiagnostic {
        uint16_t request_id;
        uint8_t conn_id;
    };
    static QueueHandle_t command_queue;
    static QueueHandle_t notification_queue;
    static QueueHandle_t sensor_queue;
    static QueueHandle_t diagnostic_queue;

    static bool enqueue(uint8_t kind, const char* value, uint16_t request_id = 0) {
        if (!command_queue) return false;
        PendingCommand command{};
        command.kind = kind;
        command.request_id = request_id;
        if (value) strncpy(command.value, value, sizeof(command.value) - 1);
        return xQueueSend(command_queue, &command, 0) == pdTRUE;
    }

    static void applyNet(const char* value) {
        // A headless BLE boot cannot become a Wi-Fi boot in place. UI Apps
        // retain their existing network controls inside the selected UI mode.
        if (RadioSvc::companionControlsMode() || current_mode == MODE_BLE ||
                current_mode == MODE_SLEEP) {
            Protocol::send_message("sys/ack", "REJECTED");
            return;
        }
        String v_str = value ? value : "";
        if (v_str == "wifi_off") {
            if (current_mode == MODE_WIFI || current_mode == MODE_UI) NetManager::setWiFi(false);
            else Protocol::send_message("sys/ack", "REJECTED");
        }
        else if (v_str.startsWith("wifi_on")) {
            int c1 = v_str.indexOf(','); int c2 = v_str.indexOf(',', c1 + 1);
            if (c1 > 0 && c2 > 0) {
                if (current_mode == MODE_WAITING) current_mode = MODE_WIFI;
                NetManager::setWiFi(true, v_str.substring(c1 + 1, c2), v_str.substring(c2 + 1));
            }
        }
    }

    static void applyMode(const char* value, uint16_t request_id = 0) {
        if (RadioSvc::companionControlsMode()) {
            Protocol::send_message("sys/ack", "REJECTED");
            return;
        }
        String v_str = value ? value : "";
        // Select once per boot. Legacy sys/mode and headless sys/comm share
        // MODE_WAITING; neither may take over an already selected mode.
        if (v_str == "BLE" && current_mode == MODE_WAITING) {
            RadioSvc::Result result = NetManager::initBLE(request_id);
            if (result == RadioSvc::QUEUED) {
                current_mode = MODE_BLE;
                Protocol::send_message("sys/ack", "QUEUED");
            } else if (result == RadioSvc::BUSY) {
                Protocol::send_message("sys/ack", "BUSY");
            } else {
                Protocol::send_message("sys/ack", "REJECTED");
            }
        } else if (v_str == "WIFI" && current_mode == MODE_WAITING) {
            current_mode = MODE_WIFI; Protocol::send_message("sys/ack", "WIFI_OK");
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
            if (request_id) Protocol::send_message("net/status", "WIFI_MODE", request_id);
#endif
        } else if (v_str == "UI" && current_mode == MODE_WAITING) {
            current_mode = MODE_UI; start_ui_mode();
        } else if (v_str == "OTA" && current_mode == MODE_WAITING) {
#if CRABIT_BW16_ENABLE_OTA
            current_mode = MODE_OTA; Protocol::send_message("sys/ack", "OTA_OK");
#else
            Protocol::send_message("sys/notify", "OTA_UNAVAILABLE");
#endif
        } else if (v_str == "EXIT" && current_mode != MODE_WAITING && current_mode != MODE_SLEEP) {
            if (is_display_active) {
                is_display_active = false;
                releaseOledBus();
                Protocol::send_message("sys/notify", "BW16_EXIT");
                AppManager::exitCurrentApp();
            }
            if (ble_was_connected || current_mode == MODE_BLE) NetManager::stopBLE();
            WiFi.disconnect();
            current_mode = MODE_SLEEP;
        } else {
            Protocol::send_message("sys/ack", "REJECTED");
        }
    }

    static void applySensors(const PendingSensors& sensors) {
        AmebaStore::imu_ax = sensors.imu[0] / 100.0f;
        AmebaStore::imu_ay = sensors.imu[1] / 100.0f;
        AmebaStore::imu_az = sensors.imu[2] / 100.0f;
        AmebaStore::opt_dx = sensors.pat[0];
        AmebaStore::opt_dy = sensors.pat[1];
        AmebaStore::opt_lifted = (sensors.pat[2] == 1);
        AmebaStore::battery_pct = sensors.battery;
        AmebaStore::raw_btn_mask = (uint8_t)sensors.buttons;

        static int last_btn = 0;
        if (sensors.buttons != 0 && sensors.buttons != last_btn) {
            if (sensors.buttons & (1 << 0)) AmebaStore::pushUiEvent(EVT_UP);
            else if (sensors.buttons & (1 << 1)) AmebaStore::pushUiEvent(EVT_DOWN);
            else if (sensors.buttons & (1 << 2)) AmebaStore::pushUiEvent(EVT_LEFT);
            else if (sensors.buttons & (1 << 3)) AmebaStore::pushUiEvent(EVT_RIGHT);
            else if (sensors.buttons & (1 << 4)) AmebaStore::pushUiEvent(EVT_OK);
            else if (sensors.buttons & (1 << 5)) AmebaStore::pushUiEvent(EVT_BACK);
        }
        last_btn = sensors.buttons;
    }

public:
    static bool enqueueMode(const CrabitWire::Message& message) {
        if (!message.request_id || (strcmp(message.value, "BLE") != 0 && strcmp(message.value, "WIFI") != 0)) return false;
        return enqueue(2, message.value, message.request_id);
    }

    static bool enqueueModePair(const CrabitWire::Message& message) {
        if (!message.request_id || strlen(message.value) >= sizeof(PendingCommand{}.value)) return false;
        return enqueue(4, message.value, message.request_id);
    }

    // Decoded by the UART worker, answered only by the existing Arduino writer.
    // This is a read-only roundtrip check, not a radio command or pairing attach.
    static bool enqueueUartProbe(const CrabitWire::Message& message) {
        if (!message.request_id || strcmp(message.value, "BLE_UART") != 0) return false;
        return enqueue(3, message.value, message.request_id);
    }

    static bool enqueueCompanion(const CrabitWire::Message& message) {
        if (!message.request_id) return false;
        char action[24] = {};
        uint32_t sequence = 0;
        if (!RadioSvc::parseCompanionValue(message.value, action, sizeof(action), &sequence))
            return RadioSvc::enqueueCompanion(message.request_id, "INVALID", 0) == RadioSvc::QUEUED;
        return RadioSvc::enqueueCompanion(message.request_id, action, sequence) == RadioSvc::QUEUED;
    }

    static bool enqueueWifiDiagnostic(uint16_t request_id, uint8_t conn_id) {
        if (!diagnostic_queue) return false;
        PendingWifiDiagnostic diagnostic{request_id, conn_id};
        return xQueueSend(diagnostic_queue, &diagnostic, 0) == pdTRUE;
    }

    static bool enqueueNotification(const CrabitWire::Message& message) {
        if (!notification_queue) return false;
        PendingNotification notification{};
        notification.request_id = message.request_id;
        strncpy(notification.topic, message.topic, sizeof(notification.topic) - 1);
        strncpy(notification.value, message.value, sizeof(notification.value) - 1);
        return xQueueSend(notification_queue, &notification, 0) == pdTRUE;
    }

    static void init() {
        command_queue = xQueueCreate(8, sizeof(PendingCommand));
        notification_queue = xQueueCreate(8, sizeof(PendingNotification));
        sensor_queue = xQueueCreate(8, sizeof(PendingSensors));
        diagnostic_queue = xQueueCreate(4, sizeof(PendingWifiDiagnostic));
        RadioSvc::init();
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
        NetworkSync::init();
#endif
        
        // 1. 核心高频流：解析并维护数据结构
        AmebaEventBus::subscribe("sns/all", [](const char* v) {
            if (!v || !sensor_queue) return;
            PendingSensors sensors{};
            if (sscanf(v, "%d,%d,%d|%d,%d,%d|%d|%d",
                       &sensors.imu[0], &sensors.imu[1], &sensors.imu[2],
                       &sensors.pat[0], &sensors.pat[1], &sensors.pat[2],
                       &sensors.battery, &sensors.buttons) == 8) {
                xQueueSend(sensor_queue, &sensors, 0);
            }
        });

        // 2. 网络控制指令
        AmebaEventBus::subscribe("sys/net", [](const char* value) {
            enqueue(1, value);
        });

        // 3. 模式切换指令
        AmebaEventBus::subscribe("sys/mode", [](const char* value) {
            enqueue(2, value);
        });

        // 4. PING
        AmebaEventBus::subscribe("sys/bw/ping", [](const char*) {
            enqueue(3, "");
        });
    }

    // Arduino/input owner only. Safe during legacy runExclusive(): consume
    // decoded sensor/input samples, never execute queued radio/mode commands.
    static void serviceInput() {
        if (!sensor_queue) return;
        PendingSensors sensors{};
        for (unsigned i = 0; i < 8; ++i) {
            if (xQueueReceive(sensor_queue, &sensors, 0) != pdTRUE) break;
            applySensors(sensors);
        }
    }

    static void service() {
        if (!command_queue || !notification_queue || !sensor_queue || !diagnostic_queue) return;
        RadioSvc::service();
#if CRABIT_BW16_ENABLE_NETWORK_SYNC
        NetworkSync::tick();
#endif
        serviceInput();
        PendingWifiDiagnostic diagnostic{};
        while (xQueueReceive(diagnostic_queue, &diagnostic, 0) == pdTRUE) {
            NetManager::sendDiagnosticWifi(diagnostic.request_id, diagnostic.conn_id);
        }
        PendingNotification notification{};
        while (xQueueReceive(notification_queue, &notification, 0) == pdTRUE) {
            char json[CrabitWire::MAX_VALUE + CrabitWire::MAX_TOPIC + 40];
            if (notification.request_id != 0) {
                snprintf(json, sizeof(json), "{\"i\":%u,\"t\":\"%s\",\"v\":\"%s\"}",
                         (unsigned)notification.request_id, notification.topic, notification.value);
            } else {
                snprintf(json, sizeof(json), "{\"t\":\"%s\",\"v\":\"%s\"}",
                         notification.topic, notification.value);
            }
            if (ble_was_connected) {
                RadioSvc::Result result =
                    NetManager::safe_ble_notify(String(json), NetManager::active_conn_id);
                if (result == RadioSvc::REJECTED) Protocol::send_message("sys/ble", "REJECTED");
            }
        }
        PendingCommand command{};
        while (xQueueReceive(command_queue, &command, 0) == pdTRUE) {
            if (command.kind == 1) applyNet(command.value);
            else if (command.kind == 2) applyMode(command.value, command.request_id);
            else if (command.kind == 3) {
                if (command.request_id && strcmp(command.value, "BLE_UART") == 0) {
                    const RadioSvc::Snapshot radio = RadioSvc::snapshot();
                    const bool ready = current_mode == MODE_BLE && radio.ram_pairing_only &&
                        (radio.state == RadioSvc::ADVERTISING || radio.state == RadioSvc::CONNECTED);
                    Protocol::send_message("sys/comm", ready ? "UART_OK" : "UART_NOT_READY",
                                           command.request_id);
                } else Protocol::send_message("sys/notify", "BW16 PONG OK!");
            } else if (command.kind == 4) {
                char action[24] = {};
                uint32_t sequence = 0;
                if (RadioSvc::parseCompanionValue(command.value, action, sizeof(action), &sequence))
                    RadioSvc::confirmModePair(command.request_id, action, sequence);
            }
        }
    }
};

#endif
