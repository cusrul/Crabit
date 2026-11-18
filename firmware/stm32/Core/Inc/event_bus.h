#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <string.h>
#include "main.h"
#include "topics.h"
#include <stdio.h>
#include "../../../shared/crabit_wire.h"
#include "../../../shared/imu_telemetry.h"
#include "global_store.h" 
#include "settings_service.h"
#include "ameba_session.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "audit_service.h"
#include "diagnostic_service.h"
#include "settings_service.h"
#include "link_queue.h"


#include "../../../shared/command_router.h"
#include "../../../shared/comms_link.h"
#include "../../../shared/feature_registry.h"
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
    inline static void (*session_status_handler)(uint16_t, const char*) = nullptr;
    inline static bool (*network_handler)(const CrabitWire::Message&) = nullptr;
    inline static void (*dirty_handler)() = nullptr;

    static void processMessage(const CrabitWire::Message& message) {
        Crabit::dispatchFeatureEvent(message.topic, message.value);
        if (AuditService::handle(message)) return;
        if (network_handler && network_handler(message)) return;
        if (strcmp(message.topic, Topics::SysComm) == 0) {
            if (session_status_handler) session_status_handler(message.request_id, message.value);
            return;
        }
        // Settings are handled before generic dispatch: same-topic correlated
        // business result, not ACK. Keep this isolated from diagnostic branches.
        if (SettingsService::handle(message)) return;
        if (DiagnosticService::handleEarly(message)) return;
        // A registered command owns its topic; otherwise the legacy subscriber
        // table still receives it. With no registrations the path is unchanged.
        if (!CrabitLink::dispatch(message.topic, message.value, message.request_id))
            dispatch(message.topic, message.value);
        if (dirty_handler) dirty_handler();
        DiagnosticService::handleLate(message);
        if (message.request_id != 0) {
            char ack_val[16];
            snprintf(ack_val, sizeof(ack_val), "%u", (unsigned)message.request_id);
            publish("ack", ack_val, message.request_id);
        }
    }



public:
    static void setNetworkHandler(bool (*handler)(const CrabitWire::Message&)) { network_handler = handler; }

    static void setSessionStatusHandler(void (*handler)(uint16_t, const char*)) {
        session_status_handler = handler;
    }

    static void setDirtyHandler(void (*handler)()) { dirty_handler = handler; }

    static bool init() {
        return LinkQueue::init();
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
        return LinkQueue::enqueueUi(message);
    }

    static void processPending(size_t max_messages = static_cast<size_t>(-1)) {
        CrabitWire::Message message;
        size_t processed = 0;
        while (processed < max_messages && LinkQueue::receiveUi(message, 0)) {
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
        return LinkQueue::send(topic, payload, request_id);
    }
};

inline Subscription EventBus::subs[MAX_SUBSCRIPTIONS];
inline int EventBus::sub_count = 0;

#endif
