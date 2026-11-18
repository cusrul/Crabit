// BW16 implementation of the shared link facade. The transport stays
// Protocol/Serial; callers only see CrabitLink.
#include "crabit_shared/comms_link.h"
#include "crabit_shared/command_router.h"
#include <atomic>

#include "../Protocol.h"

namespace {
std::atomic<uint32_t> sent{0}, dropped{0}, invalid{0};
}

bool CrabitLink::send(const char* topic, const char* value, RequestId request_id) {
    if (topic == nullptr || value == nullptr || topic[0] == 0) {
        invalid.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    if (!Protocol::send_message_checked(topic, value, request_id)) {
        dropped.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    sent.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void CrabitLink::sendBestEffort(const char* topic, const char* value, RequestId request_id) {
    (void)send(topic, value, request_id);
}

CrabitLink::Stats CrabitLink::stats() {
    return {sent.load(std::memory_order_relaxed), dropped.load(std::memory_order_relaxed),
            invalid.load(std::memory_order_relaxed), 0};
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

