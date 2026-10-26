#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <string.h>
#include <stdio.h>
#include "../../../shared/crabit_wire.h"
#include "../../../shared/imu_telemetry.h"
#include "usart.h" 
#include "global_store.h" 
#include "sensor_snapshot.h"
#include "sys_config.h"
#include "ameba_session.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "usb_script.h"
#include "../../../shared/audit_spool.h"

extern "C" void USB_LOG(const char* format, ...);
extern "C" void OLED_WR_Byte(uint8_t dat, uint8_t cmd);
extern volatile bool g_coproc_active;

typedef void (*TopicHandler)(const char* payload);

struct Subscription {
    const char* topic;
    TopicHandler handler;
};

#define MAX_SUBSCRIPTIONS 20
// Short control frames do not need a 512-byte JSON slot.
#define MAX_TX_MSG_LEN 256

class EventBus {
private:
    static Subscription subs[MAX_SUBSCRIPTIONS];
    static int sub_count;
    static QueueHandle_t ui_queue;
    inline static void (*session_status_handler)(uint16_t, const char*) = nullptr;
    inline static bool (*network_handler)(const CrabitWire::Message&) = nullptr;
    inline static CrabitAudit::Store audit_store;

    static void processMessage(const CrabitWire::Message& message) {
        if (strncmp(message.topic, "audit/", 6) == 0) {
            char reply[CrabitWire::MAX_VALUE + 1];
            if (strcmp(message.topic, CrabitAudit::PutTopic) == 0)
                audit_store.put(message.request_id, message.value, reply, sizeof(reply));
            else if (strcmp(message.topic, CrabitAudit::ReadTopic) == 0)
                audit_store.read(message.value, reply, sizeof(reply));
            else if (strcmp(message.topic, CrabitAudit::AckTopic) == 0)
                audit_store.acknowledge(message.value, reply, sizeof(reply));
            else
                snprintf(reply, sizeof(reply), "unsupported");
            publish(message.topic, reply, message.request_id);
            return;
        }
        if (network_handler && network_handler(message)) return;
        if (strcmp(message.topic, "sys/comm") == 0) {
            if (session_status_handler) session_status_handler(message.request_id, message.value);
            return;
        }
        // Settings are handled before generic dispatch: same-topic correlated
        // business result, not ACK. Keep this isolated from diagnostic branches.
        if (processSettings(message)) return;
        if (processPreferences(message)) return;
        if (strcmp(message.topic, "diag/battery") == 0) {
            if (message.value_len != 4 || memcmp(message.value, "read", 4) != 0) {
                publish(message.topic, "invalid", message.request_id);
                return;
            }
            StmSensorSnapshot snapshot{};
            const bool available = STM32_ReadSensorSnapshot(&snapshot);
            const auto& battery = snapshot.battery;
            const bool valid = available && battery.is_valid && isfinite(battery.voltage) &&
                battery.voltage > 0.5f && battery.voltage < 6.0f &&
                battery.percent >= 0 && battery.percent <= 100;
            char reply[128];
            snprintf(reply, sizeof(reply), "mv=%u;pct=%u;charging=%u;valid=%u;at=%lu;age=%lu",
                valid ? unsigned(battery.voltage * 1000.0f + 0.5f) : 0u,
                valid ? unsigned(battery.percent) : 0u, battery.is_charging ? 1u : 0u,
                valid ? 1u : 0u, (unsigned long)battery.timestamp,
                (unsigned long)(uint32_t(HAL_GetTick() - battery.timestamp)));
            publish(message.topic, reply, message.request_id);
            return;
        }
        if (strcmp(message.topic, "app/usb") == 0) {
            char reply[CrabitWire::MAX_VALUE + 1];
            CrabitUsb_ScriptHandle(message.value, message.value_len, reply, sizeof(reply));
            publish(message.topic, reply, message.request_id);
            return;
        }
        // The old script-storage topic is intentionally unavailable on this
        // product path; never forward remote payloads into the Flash armory.
        if (strcmp(message.topic, "app/badusb") == 0) {
            publish(message.topic, "UNSUPPORTED", message.request_id);
            return;
        }
        dispatch(message.topic, message.value);
        if (WindowManager::instance) WindowManager::instance->markDirty();
        if (strcmp(message.topic, "diag/echo") == 0) {
            publish("diag/echo", message.value, message.request_id);
        } else if (strcmp(message.topic, "diag/imu") == 0) {
            StmSensorSnapshot snapshot{};
            char value[192];
            const bool available = STM32_ReadSensorSnapshot(&snapshot);
            if (Crabit::formatImuTilt(value, sizeof(value), snapshot.imu.ax,
                    snapshot.imu.ay, snapshot.imu.az,
                    available && snapshot.imu.is_valid,
                    snapshot.imu.timestamp, HAL_GetTick())) {
                publish("diag/imu", value, message.request_id);
            }
        } else if (strcmp(message.topic, "diag/snapshot") == 0) {
            StmSensorSnapshot snapshot{};
            char value[CrabitWire::MAX_VALUE + 1];
            if (STM32_ReadSensorSnapshot(&snapshot)) {
                const int written = snprintf(value, sizeof(value),
                         "imu=%.3f,%.3f,%.3f;opt=%ld,%ld,%d;bmp=%.2f,%.2f;bat=%d;at=%lu;age=%lu",
                         (double)snapshot.imu.ax, (double)snapshot.imu.ay,
                         (double)snapshot.imu.az, (long)snapshot.optical.dx,
                         (long)snapshot.optical.dy, snapshot.optical.lifted ? 1 : 0,
                         (double)snapshot.pressure.pressure,
                         (double)snapshot.pressure.temperature,
                         snapshot.battery.percent, (unsigned long)snapshot.published_at,
                         (unsigned long)(uint32_t(HAL_GetTick() - snapshot.published_at)));
                if (written < 0 || (size_t)written >= sizeof(value)) {
                    snprintf(value, sizeof(value), "unavailable;at=%lu;age=%lu",
                             (unsigned long)snapshot.published_at,
                             (unsigned long)(uint32_t(HAL_GetTick() - snapshot.published_at)));
                }
            } else {
                strncpy(value, "unavailable", sizeof(value) - 1);
                value[sizeof(value) - 1] = '\0';
            }
            publish("diag/snapshot", value, message.request_id);
        }
        if (message.request_id != 0) {
            char ack_val[16];
            snprintf(ack_val, sizeof(ack_val), "%u", (unsigned)message.request_id);
            publish("ack", ack_val, message.request_id);
        }
    }

    static bool processPreferences(const CrabitWire::Message& message) {
        using namespace CrabitPreferences;
        const bool read = strcmp(message.topic, READ_TOPIC) == 0;
        const bool set = strcmp(message.topic, SET_TOPIC) == 0;
        if (!read && !set) return false;
        const char* result = "invalid";
        const char* error = "payload";
        if (read && message.value_len == 4 && memcmp(message.value, "read", 4) == 0) {
            result = "snapshot"; error = "none";
        } else if (set && Crabit::amebaTransitionActive()) {
            result = "busy"; error = "owner";
        } else if (set) {
            Patch patch;
            if (parse(message.value, message.value_len, patch)) {
                applyRecord(patch);
                if (patch.fields & (1u << LiftKey)) sys_cfg.enable_lift_wake = patch.values[LiftKey];
                if (patch.fields & (1u << TapKey)) sys_cfg.enable_tap_wake = patch.values[TapKey];
                if (patch.fields & (1u << FaceKey)) sys_cfg.watch_face = patch.values[FaceKey];
                GlobalStore.sys_volume = current.volume;
                const auto saved = ConfigManager::save();
                result = saved == ConfigManager::SaveResult::Saved ? "saved" : "save_failed";
                error = ConfigManager::saveError(saved);
                if (WindowManager::instance) WindowManager::instance->markDirty();
            }
        }
        char reply[CrabitWire::MAX_VALUE + 1];
        if (response(reply, sizeof(reply), result, error, sys_cfg.enable_lift_wake,
                     sys_cfg.enable_tap_wake, sys_cfg.watch_face, false))
            publish(message.topic, reply, message.request_id);
        return true;
    }

    static bool processSettings(const CrabitWire::Message& message) {
        const bool read = strcmp(message.topic, CrabitSettings::READ_TOPIC) == 0;
        const bool set = strcmp(message.topic, CrabitSettings::SET_TOPIC) == 0;
        if (!read && !set) return false;
        const char* result = "invalid";
        const char* error = "payload";
        bool display_deferred = false;
        if (read && message.value_len == 4 && memcmp(message.value, "read", 4) == 0) {
            result = "snapshot"; error = "none";
        } else if (set && Crabit::amebaTransitionActive()) {
            result = "busy"; error = "owner";
        } else if (set) {
            CrabitSettings::Patch patch;
            if (CrabitSettings::parse(message.value, message.value_len, patch)) {
                display_deferred = (patch.fields & (CrabitSettings::Brightness | CrabitSettings::Invert)) &&
                    (Crabit::amebaSessionActive() || g_coproc_active);
                const auto saved = ConfigManager::apply(patch);
                result = saved == ConfigManager::SaveResult::Saved ? "saved" : "save_failed";
                error = ConfigManager::saveError(saved);
                // Existing SettingsApp contrast/precharge/VCOM/invert sequence,
                // using CURRENT RAM config, only while STM32 owns OLED. Do not
                // wake the panel, switch buses, or touch BW16's handed-over OLED.
                if (!g_coproc_active && !Crabit::amebaSessionActive() &&
                    (patch.fields & (CrabitSettings::Brightness | CrabitSettings::Invert))) {
                    const float b = sys_cfg.brightness;
                    if (isfinite(b) && b >= 0 && b <= 1) {
                        OLED_WR_Byte(0x81, 0); OLED_WR_Byte(uint8_t(b * 255.0f), 0);
                        OLED_WR_Byte(0xD9, 0); OLED_WR_Byte(b < 0.15f ? 0x11 : 0x22, 0);
                        OLED_WR_Byte(0xDB, 0); OLED_WR_Byte(b < 0.15f ? 0x00 : 0x20, 0);
                    }
                    OLED_WR_Byte(sys_cfg.invert_disp ? 0xA7 : 0xA6, 0);
                }
                if (WindowManager::instance) WindowManager::instance->markDirty();
            }
        }
        const char* display = "unchanged";
        if (display_deferred) display = "deferred";
        char value[CrabitWire::MAX_VALUE + 1];
        if (ConfigManager::settingsResponse(value, sizeof(value), result, error, display)) {
            publish(message.topic, value, message.request_id);
        }
        return true;
    }

public:
    static QueueHandle_t tx_queue;
    static void setNetworkHandler(bool (*handler)(const CrabitWire::Message&)) { network_handler = handler; }

    static void setSessionStatusHandler(void (*handler)(uint16_t, const char*)) {
        session_status_handler = handler;
    }

    static bool init() {
        tx_queue = xQueueCreate(3, sizeof(CrabitWire::Message));
        ui_queue = xQueueCreate(3, sizeof(CrabitWire::Message));
        return tx_queue != NULL && ui_queue != NULL;
    }

    static bool subscribe(const char* topic, TopicHandler handler) {
        if (sub_count >= MAX_SUBSCRIPTIONS) return false;
        subs[sub_count].topic = topic;
        subs[sub_count].handler = handler;
        sub_count++;
        return true;
    }

    static void dispatch(const char* topic, const char* payload) {
        for (int i = 0; i < sub_count; i++) {
            if (subs[i].topic && strcmp(subs[i].topic, topic) == 0) {
                subs[i].handler(payload);
            }
        }
    }

    static bool enqueueMessage(const CrabitWire::Message& message) {
        if (ui_queue == NULL) return false;
        return xQueueSend(ui_queue, &message, 0) == pdPASS;
    }

    static void processPending(size_t max_messages = static_cast<size_t>(-1)) {
        if (ui_queue == NULL) return;
        CrabitWire::Message message;
        size_t processed = 0;
        while (processed < max_messages && xQueueReceive(ui_queue, &message, 0) == pdPASS) {
            processMessage(message);
            ++processed;
        }
    }

    static void publish(const char* topic, const char* payload) {
        publish(topic, payload, 0);
    }

    static void publish(const char* topic, const char* payload, uint16_t request_id) {
        (void)tryPublish(topic, payload, request_id);
    }

    static bool tryPublish(const char* topic, const char* payload, uint16_t request_id) {
        if (tx_queue == NULL) return false;
        CrabitWire::Message message = {};
        uint8_t payload_buf[CrabitWire::PAYLOAD_SIZE];
        size_t payload_len = 0;
        if (!CrabitWire::encode_message(topic, payload, request_id, payload_buf,
                                         sizeof(payload_buf), &payload_len)) return false;
        if (!CrabitWire::decode_message(payload_buf, payload_len, &message)) return false;

        if (__get_IPSR() != 0) { 
            BaseType_t xHigherPriorityTaskWoken = pdFALSE;
            const BaseType_t sent = xQueueSendFromISR(tx_queue, &message, &xHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
            return sent == pdPASS;
        } else {
            return xQueueSend(tx_queue, &message, pdMS_TO_TICKS(10)) == pdPASS;
        }
    }
};

inline Subscription EventBus::subs[MAX_SUBSCRIPTIONS];
inline int EventBus::sub_count = 0;
inline QueueHandle_t EventBus::tx_queue = NULL;
inline QueueHandle_t EventBus::ui_queue = NULL;

#endif
