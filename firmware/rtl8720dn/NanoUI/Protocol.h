#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <Arduino.h>
#include "crabit_wire.h"

// 与实现一致的 namespace 包裹。
namespace Protocol {
    bool send_message_checked(const char* topic, const char* payload, uint16_t request_id);
    void send_to_stm32(const char* payload);
    void send_message(const char* topic, const char* payload);
    void send_message(const char* topic, const char* payload, uint16_t request_id);
    void store_background_msg(String topic, String payload);
}

#endif
