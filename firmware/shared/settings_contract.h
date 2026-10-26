#ifndef CRABIT_SETTINGS_CONTRACT_H
#define CRABIT_SETTINGS_CONTRACT_H

#include "crabit_wire.h"
#include <stdio.h>

// ASCII key/value contract, deliberately safe through the existing BW16 JSON
// bridge. brightness is integer thousandths (0..1000), invert is 0/1, times
// are milliseconds. deep_sleep_ms=0 disables deep sleep. The 2026-10-22
// SysPower fix makes screen_off_ms=0 inhibit automatic screen-off; older
// firmware did not implement that behavior. The wire format is unchanged.
// Unknown/duplicate/empty fields fail atomically.
namespace CrabitSettings {
static const char READ_TOPIC[] = "sys/settings";
static const char SET_TOPIC[] = "sys/settings/set";
static const uint32_t MAX_DELAY_MS = 86400000; // 24h; avoids power sum overflow
enum Field : uint8_t { Brightness = 1, Invert = 2, ScreenOff = 4, DeepSleep = 8 };
struct Patch {
    uint8_t fields = 0;
    uint32_t brightness = 0, invert = 0, screen_off_ms = 0, deep_sleep_ms = 0;
};

inline bool equal(const char* text, size_t size, const char* expected) {
    return strlen(expected) == size && memcmp(text, expected, size) == 0;
}

inline bool parse(const char* value, size_t size, Patch& out) {
    Patch patch{};
    if (!value || size == 0 || size > CrabitWire::MAX_VALUE) return false;
    size_t pos = 0;
    while (pos < size) {
        const size_t start = pos;
        while (pos < size && value[pos] != '=') ++pos;
        if (pos == size) return false;
        const size_t key_size = pos++ - start;
        uint8_t field = 0;
        uint32_t limit = MAX_DELAY_MS;
        uint32_t* target = nullptr;
        if (equal(value + start, key_size, "brightness")) {
            field = Brightness; target = &patch.brightness; limit = 1000;
        } else if (equal(value + start, key_size, "invert")) {
            field = Invert; target = &patch.invert; limit = 1;
        } else if (equal(value + start, key_size, "screen_off_ms")) {
            field = ScreenOff; target = &patch.screen_off_ms;
        } else if (equal(value + start, key_size, "deep_sleep_ms")) {
            field = DeepSleep; target = &patch.deep_sleep_ms;
        } else return false;
        if ((patch.fields & field) != 0 || pos == size) return false;
        uint32_t number = 0;
        const size_t digits = pos;
        while (pos < size && value[pos] != ';') {
            const char c = value[pos++];
            if (c < '0' || c > '9' || number > (limit - uint32_t(c - '0')) / 10)
                return false;
            number = number * 10 + uint32_t(c - '0');
            if (number > limit) return false;
        }
        if (pos == digits || (field == ScreenOff && number == 0)) return false;
        *target = number;
        patch.fields |= field;
        if (pos < size && ++pos == size) return false;
    }
    out = patch;
    return true;
}

// A snapshot can contain historical/custom values outside the editable range.
// brightness=-1 reports invalid legacy RAM instead of silently inventing a value.
inline bool response(char* out, size_t capacity, const char* result,
                     const char* error, const char* display, int brightness, bool invert,
                     uint32_t screen_off_ms, uint32_t deep_sleep_ms) {
    if (!out || !capacity) return false;
    const int n = snprintf(out, capacity,
        "result=%s;error=%s;display=%s;brightness=%d;invert=%u;screen_off_ms=%lu;deep_sleep_ms=%lu",
        result, error, display, brightness, invert ? 1u : 0u,
        (unsigned long)screen_off_ms, (unsigned long)deep_sleep_ms);
    return n > 0 && size_t(n) < capacity && size_t(n) <= CrabitWire::MAX_VALUE;
}
}
#endif
