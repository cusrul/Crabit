#pragma once

#include "crabit_wire.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace CrabitPreferences {
constexpr const char* READ_TOPIC = "sys/prefs";
constexpr const char* SET_TOPIC = "sys/prefs/set";
enum Flag : uint8_t { Pat = 1, Qmi = 2, Sound = 4, Led = 8 };

// An optional trailer AFTER the unmodified 48-byte legacy config. Old firmware
// ignores it; an old-firmware save erases it, so new firmware falls back safely.
struct Record {
    uint32_t magic = 0x31524650u;  // PFR1
    uint8_t flags = Pat | Qmi | Sound | Led;
    uint8_t volume = 100;
    uint8_t led_level = 8;
    uint8_t checksum = 0;
};
static_assert(sizeof(Record) == 8, "one STM32 Flash doubleword");
inline Record current{};
inline bool enabled(Flag flag) {
    return (current.flags & flag) != 0;
}
inline uint8_t checksum(const Record& record) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(&record);
    uint8_t crc = 0;
    for (unsigned i = 0; i < 7; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80) ? uint8_t((crc << 1) ^ 0x07) : uint8_t(crc << 1);
    }
    return crc;
}
inline bool valid(const Record& record) {
    return record.magic == 0x31524650u && (record.flags & ~15u) == 0 && record.volume <= 100 &&
           record.led_level <= 100 && record.checksum == checksum(record);
}
inline void load(const Record& record) {
    current = valid(record) ? record : Record{};
}
inline Record sealed() {
    Record r = current;
    r.checksum = checksum(r);
    return r;
}

enum Key : uint8_t {
    PatKey,
    QmiKey,
    LiftKey,
    TapKey,
    SoundKey,
    LedKey,
    VolumeKey,
    LedLevelKey,
    FaceKey,
    Count
};
constexpr const char* keys[Count] = {"pat", "qmi",    "lift",      "tap", "sound",
                                     "led", "volume", "led_level", "face"};
struct Patch {
    uint16_t fields = 0;
    uint8_t values[Count]{};
};
inline bool parse(const char* text, size_t size, Patch& out) {
    if (!text || size == 0 || size > CrabitWire::MAX_VALUE)
        return false;
    Patch patch{};
    size_t pos = 0;
    while (pos < size) {
        const size_t start = pos;
        while (pos < size && text[pos] != '=')
            ++pos;
        if (pos == size)
            return false;
        unsigned key = Count;
        for (unsigned i = 0; i < Count; ++i)
            if (strlen(keys[i]) == pos - start && memcmp(text + start, keys[i], pos - start) == 0)
                key = i;
        if (key == Count || (patch.fields & (1u << key)))
            return false;
        ++pos;
        const size_t digits = pos;
        unsigned value = 0;
        const unsigned limit = key == FaceKey ? 4u : key >= VolumeKey ? 100u : 1u;
        while (pos < size && text[pos] != ';') {
            const char c = text[pos++];
            if (c < '0' || c > '9' || value > 100)
                return false;
            value = value * 10 + unsigned(c - '0');
            if (value > limit)
                return false;
        }
        if (pos == digits)
            return false;
        patch.values[key] = uint8_t(value);
        patch.fields |= uint16_t(1u << key);
        if (pos < size && ++pos == size)
            return false;
    }
    out = patch;
    return true;
}
inline void applyRecord(const Patch& p) {
    constexpr Key flag_keys[] = {PatKey, QmiKey, SoundKey, LedKey};
    constexpr Flag flags[] = {Pat, Qmi, Sound, Led};
    for (unsigned i = 0; i < 4; ++i)
        if (p.fields & (1u << flag_keys[i])) {
            if (p.values[flag_keys[i]])
                current.flags |= flags[i];
            else
                current.flags &= uint8_t(~flags[i]);
        }
    if (p.fields & (1u << VolumeKey))
        current.volume = p.values[VolumeKey];
    if (p.fields & (1u << LedLevelKey))
        current.led_level = p.values[LedLevelKey];
}
inline bool response(char* out, size_t capacity, const char* result, const char* error, bool lift,
                     bool tap, unsigned face, bool deep_sleep) {
    const int n =
        snprintf(out, capacity,
                 "result=%s;error=%s;pat=%u;qmi=%u;lift=%u;tap=%u;sound=%u;led=%u;volume=%u;led_"
                 "level=%u;face=%u;deep_sleep=%u",
                 result, error, enabled(Pat) ? 1u : 0u, enabled(Qmi) ? 1u : 0u, lift ? 1u : 0u,
                 tap ? 1u : 0u, enabled(Sound) ? 1u : 0u, enabled(Led) ? 1u : 0u,
                 unsigned(current.volume), unsigned(current.led_level), face, deep_sleep ? 1u : 0u);
    return n > 0 && size_t(n) < capacity && size_t(n) <= CrabitWire::MAX_VALUE;
}
}  // namespace CrabitPreferences
