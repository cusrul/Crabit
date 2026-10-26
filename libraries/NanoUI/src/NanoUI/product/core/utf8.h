#pragma once

#include <stdint.h>

namespace NanoUI {
namespace detail {

// Decode one scalar from a NUL-terminated string. End/null returns zero without
// advancing. Invalid input consumes one byte and returns U+FFFD, preserving the
// existing byte-by-byte recovery policy. No allocation or platform API is used.
inline uint32_t decodeUtf8(const char** cursor) {
    if (!cursor || !*cursor) {
        return 0;
    }

    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(*cursor);
    const uint8_t lead = bytes[0];
    if (lead == 0) {
        return 0;
    }
    if (lead < 0x80) {
        ++*cursor;
        return lead;
    }

    uint32_t scalar = 0;
    uint32_t minimum = 0;
    uint8_t length = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        scalar = lead & 0x1F;
        minimum = 0x80;
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        scalar = lead & 0x0F;
        minimum = 0x800;
        length = 3;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        scalar = lead & 0x07;
        minimum = 0x10000;
        length = 4;
    } else {
        ++*cursor;
        return 0xFFFD;
    }

    for (uint8_t index = 1; index < length; ++index) {
        const uint8_t next = bytes[index];
        // NUL is not a continuation byte. Return immediately so a truncated
        // sequence never reads the next byte beyond its string terminator.
        if ((next & 0xC0) != 0x80) {
            ++*cursor;
            return 0xFFFD;
        }
        scalar = (scalar << 6) | (next & 0x3F);
    }

    if (scalar < minimum || scalar > 0x10FFFF || (scalar >= 0xD800 && scalar <= 0xDFFF)) {
        ++*cursor;
        return 0xFFFD;
    }

    *cursor += length;
    return scalar;
}

}  // namespace detail
}  // namespace NanoUI
