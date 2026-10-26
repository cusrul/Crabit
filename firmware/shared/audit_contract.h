#ifndef CRABIT_AUDIT_CONTRACT_H
#define CRABIT_AUDIT_CONTRACT_H

#include <stddef.h>
#include <stdint.h>

// Small wire contract mirrored for Arduino IDE's isolated sketch build.
namespace CrabitAudit {
constexpr size_t MaxRecord = 768;
constexpr size_t ChunkBytes = 64;
constexpr size_t Capacity = 2;
constexpr const char* PutTopic = "audit/put";
constexpr const char* ReadTopic = "audit/read";
constexpr const char* AckTopic = "audit/ack";

inline bool number(const char*& p, uint32_t& result, char delimiter) {
    result = 0;
    if (*p < '0' || *p > '9')
        return false;
    do {
        const unsigned digit = unsigned(*p++ - '0');
        if (result > (UINT32_MAX - digit) / 10)
            return false;
        result = result * 10 + digit;
    } while (*p >= '0' && *p <= '9');
    if (*p != delimiter)
        return false;
    if (delimiter)
        ++p;
    return true;
}

inline int nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    return -1;
}

inline void hex(const char* bytes, size_t length, char* out) {
    constexpr char digits[] = "0123456789ABCDEF";
    for (size_t i = 0; i < length; ++i) {
        const uint8_t c = uint8_t(bytes[i]);
        *out++ = digits[c >> 4];
        *out++ = digits[c & 15];
    }
    *out = 0;
}
}  // namespace CrabitAudit

#endif
