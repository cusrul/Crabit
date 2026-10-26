#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "audit_contract.h"

// Wire values stay below CrabitWire v1's 191-byte limit. No Flash or SDK calls.
namespace CrabitAudit {
class Store {
    struct Record {
        uint32_t id = 0;
        uint16_t size = 0;
        char bytes[MaxRecord]{};
    } records[Capacity];
    char staging[MaxRecord]{};
    uint16_t source = 0, received = 0, expected = 0;
    uint16_t lastSource = 0;
    uint32_t nextId = 1, lastStored = 0, lastAck = 0, dropped = 0;
    size_t head = 0, count = 0;

   public:
    // Called only by STM32 EventBus/UI owner. UART worker merely queues messages.
    void put(uint16_t request, const char* value, char* reply, size_t capacity) {
        const char* p = value;
        uint32_t offset = 0, total = 0;
        if (!request || !number(p, offset, ':') || !number(p, total, ':') || !total ||
            total > MaxRecord || offset >= total) {
            snprintf(reply, capacity, "invalid");
            return;
        }
        const size_t digits = strlen(p);
        if (!digits || digits % 2 || digits > ChunkBytes * 2 || digits / 2 > total - offset) {
            snprintf(reply, capacity, "invalid");
            return;
        }
        char bytes[ChunkBytes];
        for (size_t i = 0; i < digits / 2; ++i) {
            const int a = nibble(p[i * 2]), b = nibble(p[i * 2 + 1]);
            if (a < 0 || b < 0 || (a == 0 && b == 0)) {
                snprintf(reply, capacity, "invalid");
                return;
            }
            bytes[i] = char((a << 4) | b);
        }
        const size_t length = digits / 2;
        // A lost final ACK must not append the same record a second time.
        if (request == lastSource && source != request && offset + length == total) {
            for (const auto& record : records) {
                if (record.id == lastStored && record.size == total &&
                    memcmp(record.bytes + offset, bytes, length) == 0) {
                    snprintf(reply, capacity, "stored:%lu", (unsigned long)lastStored);
                    return;
                }
            }
        }
        if (offset == 0) {
            source = request;
            received = 0;
            expected = uint16_t(total);
        }
        if (request != source || total != expected || offset > received ||
            (offset < received &&
             (offset + length > received || memcmp(staging + offset, bytes, length) != 0))) {
            snprintf(reply, capacity, "invalid");
            return;
        }
        if (count == Capacity) {
            if (offset == 0)
                ++dropped;
            snprintf(reply, capacity, "full");
            return;
        }
        if (offset == received) {
            memcpy(staging + received, bytes, length);
            received += uint16_t(length);
        }
        if (received == expected) {
            Record& record = records[(head + count) % Capacity];
            record.id = nextId++;
            if (!nextId)
                nextId = 1;
            record.size = received;
            memcpy(record.bytes, staging, received);
            ++count;
            lastSource = request;
            lastStored = record.id;
            source = received = expected = 0;
            snprintf(reply, capacity, "stored:%lu", (unsigned long)record.id);
        } else {
            snprintf(reply, capacity, "next:%u", unsigned(received));
        }
    }

    void read(const char* value, char* reply, size_t capacity) const {
        if (strcmp(value, "head") == 0) {
            snprintf(reply, capacity, "id=%lu;size=%u;pending=%u;dropped=%lu",
                     count ? (unsigned long)records[head].id : 0ul,
                     count ? unsigned(records[head].size) : 0u, unsigned(count),
                     (unsigned long)dropped);
            return;
        }
        const char* p = value;
        uint32_t id = 0, offset = 0;
        if (!number(p, id, ':') || !number(p, offset, 0) || !count || records[head].id != id ||
            offset >= records[head].size || capacity < ChunkBytes * 2 + 1) {
            snprintf(reply, capacity, "invalid");
            return;
        }
        const Record& record = records[head];
        size_t length = record.size - offset;
        if (length > ChunkBytes)
            length = ChunkBytes;
        hex(record.bytes + offset, length, reply);
    }

    void acknowledge(const char* value, char* reply, size_t capacity) {
        const char* p = value;
        uint32_t id = 0;
        if (!number(p, id, 0) || !id) {
            snprintf(reply, capacity, "invalid");
        } else if (count && records[head].id == id) {
            records[head].id = 0;
            records[head].size = 0;
            head = (head + 1) % Capacity;
            --count;
            lastAck = id;
            snprintf(reply, capacity, "ok");
        } else {
            snprintf(reply, capacity, "%s", id == lastAck ? "ok" : "invalid");
        }
    }
};
static_assert(sizeof(Store) < 2500, "Keep the initial STM32 audit buffer small");
}  // namespace CrabitAudit
