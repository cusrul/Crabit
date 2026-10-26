#include "AuditRelay.h"
#include "Protocol.h"
#include "audit_contract.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <stdlib.h>

namespace AuditRelay {
namespace {
struct Reply {
    uint16_t request;
    char value[32];
};
QueueHandle_t records = nullptr, replies = nullptr;
char* active = nullptr;
size_t offset = 0, length = 0;
uint16_t request = 0;
uint32_t sentAt = 0;
unsigned retries = 0, storedCount = 0, droppedCount = 0;
bool waiting = false;
const char* currentStatus = "等待记录";

void discard() {
    free(active);
    active = nullptr;
    waiting = false;
}
void countDrop() {
    taskENTER_CRITICAL();
    ++droppedCount;
    taskEXIT_CRITICAL();
}
}  // namespace

void begin() {
    // Initialize on the UI task, before enabling capture callbacks.
    if (!records)
        records = xQueueCreate(4, sizeof(char*));
    if (!replies)
        replies = xQueueCreate(2, sizeof(Reply));
}

bool enqueue(const char* json) {
    if (!json)
        return false;
    const size_t size = strlen(json);
    if (!records || !replies || !size || size > CrabitAudit::MaxRecord) {
        countDrop();
        return false;
    }
    char* copy = static_cast<char*>(malloc(size + 1));
    if (!copy) {
        countDrop();
        return false;
    }
    memcpy(copy, json, size + 1);
    if (xQueueSend(records, &copy, 0) == pdPASS)
        return true;
    free(copy);
    countDrop();
    return false;
}

void receive(const CrabitWire::Message& message) {
    if (!replies || message.value_len >= sizeof(Reply::value))
        return;
    Reply reply{};
    reply.request = message.request_id;
    memcpy(reply.value, message.value, message.value_len);
    xQueueSend(replies, &reply, 0);
}

void tick() {
    if (!records || !replies)
        return;
    Reply reply{};
    while (xQueueReceive(replies, &reply, 0) == pdPASS) {
        if (!active || !waiting || reply.request != request)
            continue;
        if (strncmp(reply.value, "stored:", 7) == 0) {
            const char* p = reply.value + 7;
            uint32_t id = 0;
            if (!CrabitAudit::number(p, id, 0) || !id || length - offset > CrabitAudit::ChunkBytes)
                continue;
            ++storedCount;
            currentStatus = "已交给主控";
            discard();
        } else if (strncmp(reply.value, "next:", 5) == 0) {
            const char* p = reply.value + 5;
            uint32_t next = 0;
            const size_t expected =
                offset + ((length - offset < CrabitAudit::ChunkBytes) ? length - offset
                                                                      : CrabitAudit::ChunkBytes);
            if (CrabitAudit::number(p, next, 0) && next == expected && next < length) {
                offset = next;
                waiting = false;
                retries = 0;
            }
        } else {
            currentStatus = strcmp(reply.value, "full") == 0 ? "主控记录已满" : "主控拒绝记录";
            countDrop();
            discard();
        }
    }
    if (!active) {
        if (xQueueReceive(records, &active, 0) != pdPASS)
            return;
        length = strlen(active);
        offset = 0;
        if (++request == 0)
            ++request;
        retries = 0;
    }
    if (waiting && uint32_t(millis() - sentAt) < 750)
        return;
    if (waiting && ++retries > 3) {
        currentStatus = "主控接收超时";
        countDrop();
        discard();
        return;
    }
    char value[160];
    const int prefix = snprintf(value, sizeof(value), "%u:%u:", unsigned(offset), unsigned(length));
    const size_t chunk =
        length - offset < CrabitAudit::ChunkBytes ? length - offset : CrabitAudit::ChunkBytes;
    CrabitAudit::hex(active + offset, chunk, value + prefix);
    Protocol::send_message_checked(CrabitAudit::PutTopic, value, request);
    waiting = true;
    sentAt = millis();
    currentStatus = "正在传给主控";
}

unsigned stored() {
    return storedCount;
}
unsigned dropped() {
    taskENTER_CRITICAL();
    const unsigned value = droppedCount;
    taskEXIT_CRITICAL();
    return value;
}
const char* status() {
    return currentStatus;
}
}  // namespace AuditRelay
