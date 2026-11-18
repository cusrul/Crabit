#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Optional feature modules (new sensors, diagnostics, storage, ...). The main
// loop calls the table once per tick; features do not need to edit the loop.
// Header-only and allocation-free.
namespace Crabit {

struct Feature {
    const char* id;
    bool (*init)();
    void (*tick)(uint32_t now_ms);
    void (*event)(const char* topic, const char* value);
};

namespace detail {
constexpr size_t kMaxFeatures = 6;

struct FeatureTable {
    Feature entries[kMaxFeatures];
    size_t count;
};

inline FeatureTable& featureTable() {
    static FeatureTable table{};
    return table;
}
}  // namespace detail

inline bool registerFeature(const Feature& feature) {
    if (feature.id == nullptr || feature.id[0] == '\0') return false;
    detail::FeatureTable& table = detail::featureTable();
    for (size_t i = 0; i < table.count; ++i)
        if (strcmp(table.entries[i].id, feature.id) == 0) return false;
    if (table.count >= detail::kMaxFeatures) return false;
    table.entries[table.count++] = feature;
    return true;
}

inline size_t featureCount() { return detail::featureTable().count; }

inline bool unregisterFeature(const char* id) {
    if (id == nullptr) return false;
    detail::FeatureTable& table = detail::featureTable();
    for (size_t i = 0; i < table.count; ++i) {
        if (strcmp(table.entries[i].id, id) != 0) continue;
        for (size_t j = i; j + 1 < table.count; ++j)
            table.entries[j] = table.entries[j + 1];
        --table.count;
        table.entries[table.count] = Feature{};
        return true;
    }
    return false;
}

// Runs every feature init; returns false if any init failed.
inline bool initFeatures() {
    detail::FeatureTable& table = detail::featureTable();
    bool ok = true;
    for (size_t i = 0; i < table.count; ++i)
        if (table.entries[i].init && !table.entries[i].init()) ok = false;
    return ok;
}

inline void tickFeatures(uint32_t now_ms) {
    detail::FeatureTable& table = detail::featureTable();
    for (size_t i = 0; i < table.count; ++i)
        if (table.entries[i].tick) table.entries[i].tick(now_ms);
}

// Fan-out to observers. Command handlers still run through the command router.
inline void dispatchFeatureEvent(const char* topic, const char* value) {
    if (topic == nullptr) return;
    detail::FeatureTable& table = detail::featureTable();
    for (size_t i = 0; i < table.count; ++i)
        if (table.entries[i].event) table.entries[i].event(topic, value);
}

}  // namespace Crabit
