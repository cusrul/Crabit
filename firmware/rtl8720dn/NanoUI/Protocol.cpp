#include "Protocol.h"
#include <stdlib.h>

// 与头文件一致的 namespace 包裹。
namespace Protocol {

    bool send_message_checked(const char* topic, const char* payload, uint16_t request_id) {
        uint8_t payload_buf[CrabitWire::PAYLOAD_SIZE];
        uint8_t frame[CrabitWire::FRAME_SIZE];
        size_t payload_len = 0;
        size_t frame_len = 0;
        if (!CrabitWire::encode_message(topic, payload, request_id, payload_buf,
                                         sizeof(payload_buf), &payload_len) ||
            !CrabitWire::encode_frame(payload_buf, payload_len, frame,
                                       sizeof(frame), &frame_len)) return false;
        const size_t written = Serial.write(frame, frame_len);
        Serial.flush();
        return written == frame_len;
    }

    void send_message(const char* topic, const char* payload, uint16_t request_id) {
        (void)send_message_checked(topic, payload, request_id);
    }

    void send_message(const char* topic, const char* payload) {
        send_message(topic, payload, 0);
    }

    static bool json_field(const char* json, const char* key, char* out, size_t capacity) {
        if (!json || !key || !out || capacity == 0) return false;
        char needle[8] = {'"', 0};
        size_t key_len = strlen(key);
        if (key_len > sizeof(needle) - 3) return false;
        memcpy(needle + 1, key, key_len);
        needle[key_len + 1] = '"';
        needle[key_len + 2] = '\0';
        const char* start = strstr(json, needle);
        if (!start) return false;
        start = strchr(start + key_len + 2, ':');
        if (!start) return false;
        while (*++start == ' ' || *start == '\t') {}
        if (*start != '"') return false;
        ++start;
        size_t length = 0;
        while (start[length] && start[length] != '"') ++length;
        if (length >= capacity) return false;
        memcpy(out, start, length);
        out[length] = '\0';
        return true;
    }

    static bool json_number_field(const char* json, const char* key,
                                  unsigned long* out) {
        if (!json || !key || !out) return false;
        char needle[8] = {'"', 0};
        size_t key_len = strlen(key);
        if (key_len > sizeof(needle) - 3) return false;
        memcpy(needle + 1, key, key_len);
        needle[key_len + 1] = '"';
        needle[key_len + 2] = '\0';
        const char* start = strstr(json, needle);
        if (!start) return false;
        start = strchr(start + key_len + 2, ':');
        if (!start) return false;
        while (*++start == ' ' || *start == '\t') {}
        char* end = NULL;
        unsigned long parsed = strtoul(start, &end, 10);
        if (end == start || parsed > 0xFFFFUL) return false;
        *out = parsed;
        return true;
    }

    bool decode_command_json(const char* payload, char* topic, size_t topic_capacity,
                             char* value, size_t value_capacity, uint16_t* request_id) {
        if (!payload || !topic || !value || !request_id) return false;
        unsigned long parsed_request_id = 0;
        if (json_number_field(payload, "i", &parsed_request_id))
            *request_id = (uint16_t)parsed_request_id;
        else
            *request_id = 0;
        return json_field(payload, "t", topic, topic_capacity) &&
               json_field(payload, "v", value, value_capacity);
    }

    void store_background_msg(String topic, String payload) {
        send_message(topic.c_str(), payload.c_str());
    }

} // end namespace Protocol
