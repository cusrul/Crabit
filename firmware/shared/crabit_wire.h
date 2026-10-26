#ifndef CRABIT_WIRE_H
#define CRABIT_WIRE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace CrabitWire {

static const uint8_t VERSION = 1;
static const size_t MAX_TOPIC = 47;
static const size_t MAX_VALUE = 191;
static const size_t PAYLOAD_SIZE = 5 + MAX_TOPIC + MAX_VALUE;
static const size_t FRAME_SIZE = 2 + PAYLOAD_SIZE + 2;

struct Message {
    uint8_t topic_len;
    uint8_t value_len;
    uint16_t request_id;
    char topic[MAX_TOPIC + 1];
    char value[MAX_VALUE + 1];
};

static_assert(sizeof(Message) <= 256, "Wire message must fit one RTOS queue slot");

inline uint16_t crc16(const uint8_t* data, size_t length) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < length; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? static_cast<uint16_t>((crc >> 1) ^ 0xA001U)
                             : static_cast<uint16_t>(crc >> 1);
        }
    }
    return crc;
}

inline bool encode_message(const char* topic, const char* value, uint16_t request_id,
                           uint8_t* out, size_t capacity, size_t* written) {
    if (!topic || !value || !out || !written) return false;
    const size_t topic_len = strlen(topic);
    const size_t value_len = strlen(value);
    if (topic_len > MAX_TOPIC || value_len > MAX_VALUE || capacity < PAYLOAD_SIZE) return false;

    out[0] = VERSION;
    out[1] = static_cast<uint8_t>(topic_len);
    out[2] = static_cast<uint8_t>(value_len);
    out[3] = static_cast<uint8_t>(request_id & 0xFFU);
    out[4] = static_cast<uint8_t>(request_id >> 8);
    memcpy(out + 5, topic, topic_len);
    memcpy(out + 5 + topic_len, value, value_len);
    *written = 5 + topic_len + value_len;
    return true;
}

inline bool decode_message(const uint8_t* data, size_t length, Message* message) {
    if (!data || !message || length < 5 || data[0] != VERSION) return false;
    const size_t topic_len = data[1];
    const size_t value_len = data[2];
    if (topic_len > MAX_TOPIC || value_len > MAX_VALUE || 5 + topic_len + value_len != length) {
        return false;
    }
    message->topic_len = static_cast<uint8_t>(topic_len);
    message->value_len = static_cast<uint8_t>(value_len);
    message->request_id = static_cast<uint16_t>(data[3]) |
                          static_cast<uint16_t>(data[4] << 8);
    memcpy(message->topic, data + 5, topic_len);
    memcpy(message->value, data + 5 + topic_len, value_len);
    message->topic[topic_len] = '\0';
    message->value[value_len] = '\0';
    return true;
}

inline bool encode_frame(const uint8_t* payload, size_t payload_length,
                         uint8_t* out, size_t capacity, size_t* written) {
    if (!payload || !out || !written || payload_length < 5 || payload_length > PAYLOAD_SIZE ||
        capacity < payload_length + 4) return false;
    out[0] = static_cast<uint8_t>(payload_length & 0xFFU);
    out[1] = static_cast<uint8_t>(payload_length >> 8);
    memcpy(out + 2, payload, payload_length);
    const uint16_t crc = crc16(payload, payload_length);
    out[2 + payload_length] = static_cast<uint8_t>(crc & 0xFFU);
    out[3 + payload_length] = static_cast<uint8_t>(crc >> 8);
    *written = payload_length + 4;
    return true;
}

inline bool decode_frame(const uint8_t* frame, size_t length, uint8_t* payload,
                         size_t capacity, size_t* payload_length) {
    if (!frame || !payload || !payload_length || length < 4) return false;
    const size_t declared = static_cast<size_t>(frame[0]) |
                            (static_cast<size_t>(frame[1]) << 8);
    if (declared < 5 || declared > PAYLOAD_SIZE || declared + 4 != length || capacity < declared) {
        return false;
    }
    const uint16_t received = static_cast<uint16_t>(frame[2 + declared]) |
                              static_cast<uint16_t>(frame[3 + declared] << 8);
    if (crc16(frame + 2, declared) != received) return false;
    memcpy(payload, frame + 2, declared);
    *payload_length = declared;
    return true;
}

}

#endif
