#pragma once

#include <stdint.h>

// The single public interface for STM32 <-> BW16 data exchange. Both cores
// implement send/subscribe on top of their transport (EventBus / Protocol),
// and each transport calls dispatch() once per decoded frame. New code must
// use this header instead of the transport internals.
class CrabitLink {
public:

    using RequestId = uint16_t;

    // Returns false when the command is already owned or the table is full.
    using Handler = bool (*)(void* ctx, const char* topic, const char* value,
                             RequestId request_id);

    struct Stats {
        uint32_t sent;
        uint32_t dropped;
        uint32_t invalid;
        uint32_t high_water;
        constexpr Stats(uint32_t s = 0, uint32_t d = 0, uint32_t i = 0, uint32_t h = 0)
            : sent(s), dropped(d), invalid(i), high_water(h) {}
    };

    // Returns false when the queue is full or the frame is invalid.
    static bool send(const char* topic, const char* value, RequestId request_id = 0);
    // Explicitly discard the result for telemetry that must never block.
    static void sendBestEffort(const char* topic, const char* value, RequestId request_id = 0);
    static Stats stats();
    static bool subscribe(const char* topic, Handler handler, void* ctx);
    static bool unsubscribe(const char* topic, void* ctx);

    // Called by the transport after a frame is decoded. Returns true when a
    // subscriber consumed the topic.
    static bool dispatch(const char* topic, const char* value, RequestId request_id);
};
