// STM32 implementation of the shared link facade. The transport stays
// EventBus; callers only see CrabitLink.
#include "../../../shared/comms_link.h"
#include <atomic>

#include "event_bus.h"
#include "link_queue.h"

namespace {
std::atomic<uint32_t> sent{0}, dropped{0}, invalid{0}, high_water{0};
}

bool CrabitLink::send(const char* topic, const char* value, RequestId request_id) {
    if (topic == nullptr || value == nullptr || topic[0] == 0) {
        invalid.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!LinkQueue::send(topic, value, request_id)) {
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sent.fetch_add(1, std::memory_order_relaxed);
    {
        const uint32_t depth = LinkQueue::txDepth();
        uint32_t current = high_water.load(std::memory_order_relaxed);
        while (depth > current &&
               !high_water.compare_exchange_weak(current, depth, std::memory_order_relaxed)) {
        }
    }
    return true;
}

void CrabitLink::sendBestEffort(const char* topic, const char* value, RequestId request_id) {
    (void)send(topic, value, request_id);
}

CrabitLink::Stats CrabitLink::stats() {
    return {sent.load(std::memory_order_relaxed), dropped.load(std::memory_order_relaxed),
            invalid.load(std::memory_order_relaxed), high_water.load(std::memory_order_relaxed)};
}

bool CrabitLink::subscribe(const char* topic, Handler handler, void* ctx) {
    return Crabit::registerCommand(topic, handler, ctx);
}

bool CrabitLink::unsubscribe(const char* topic, void* ctx) {
    return Crabit::unregisterCommand(topic, ctx);
}

bool CrabitLink::dispatch(const char* topic, const char* value, RequestId request_id) {
    return Crabit::dispatchCommand(topic, value, request_id);
}

