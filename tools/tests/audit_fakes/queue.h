#pragma once
#include <cstring>
#include <deque>
#include <vector>
struct AuditFakeQueue {
    size_t capacity, width;
    std::deque<std::vector<unsigned char>> items;
};
using QueueHandle_t = AuditFakeQueue*;
inline QueueHandle_t xQueueCreate(size_t capacity, size_t width) {
    return new AuditFakeQueue{capacity, width, {}};
}
inline int xQueueSend(QueueHandle_t queue, const void* item, unsigned) {
    if (queue->items.size() >= queue->capacity)
        return 0;
    const auto* bytes = static_cast<const unsigned char*>(item);
    queue->items.emplace_back(bytes, bytes + queue->width);
    return 1;
}
inline int xQueueReceive(QueueHandle_t queue, void* item, unsigned) {
    if (queue->items.empty())
        return 0;
    memcpy(item, queue->items.front().data(), queue->width);
    queue->items.pop_front();
    return 1;
}
