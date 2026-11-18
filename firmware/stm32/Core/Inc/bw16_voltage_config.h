#pragma once

#include <stdint.h>

// BW16 DCDC3 target voltage, stored as an optional 8-byte trailer AFTER the
// unchanged 48-byte SystemConfig and the 8-byte CrabitPreferences trailer.
// The UI only records the target here; the PMU is written exclusively by the
// existing BW16 boot helper through targetMv().
namespace Crabit {
namespace Bw16Voltage {

// 0x31564257 is "WBV1" in a little-endian Flash dump.
inline constexpr uint32_t MAGIC = 0x31564257u;
inline constexpr uint8_t VERSION = 1;

inline constexpr uint16_t levels[] = {3000, 3100, 3200};
inline constexpr uint8_t level_count = sizeof(levels) / sizeof(levels[0]);
static_assert(levels[0] == 3000 && levels[1] == 3100 && levels[2] == 3200,
              "only the three supported BW16 voltages may reach the power call");

struct Record {
    uint32_t magic = MAGIC;
    uint8_t version = VERSION;
    uint8_t code = 0;  // Index into levels[]; never a raw millivolt value.
    uint8_t reserved = 0;
    uint8_t checksum = 0;  // Last byte so the CRC covers every other byte.
};
static_assert(sizeof(Record) == 8, "one STM32 Flash doubleword");

// The loaded/edited target. Kept in RAM until the existing ConfigManager save.
inline Record current{};

// Same CRC-8 polynomial as the preference trailer.
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
    return record.magic == MAGIC && record.version == VERSION &&
           record.code < level_count && record.reserved == 0 &&
           record.checksum == checksum(record);
}

// A missing, erased, truncated, old-format or corrupt trailer is 3.0 V.
inline void load(const Record& record) { current = valid(record) ? record : Record{}; }

inline Record sealed() {
    Record record = current;
    record.checksum = checksum(record);
    return record;
}

inline uint8_t index() { return current.code < level_count ? current.code : 0; }

inline uint16_t targetMv() { return levels[index()]; }

inline bool setIndex(uint8_t value) {
    if (value >= level_count)
        return false;
    current.magic = MAGIC;
    current.version = VERSION;
    current.code = value;
    current.reserved = 0;
    return true;
}

}  // namespace Bw16Voltage
}  // namespace Crabit
