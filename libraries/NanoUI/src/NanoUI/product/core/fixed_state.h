#ifndef NANOUI_FIXED_STATE_H
#define NANOUI_FIXED_STATE_H

#include <stdint.h>
#include <stddef.h>
#include "config.h"
#if NANOUI_DYNAMIC_STORAGE
#include <map>
#include <string>
#endif

namespace NanoUI {

#if NANOUI_DYNAMIC_STORAGE
using StateKey = std::string;
inline StateKey makeStateKey(const char* text) {
    return text ? text : "";
}
inline bool stateKeyValid(const StateKey& key) {
    return !key.empty();
}
#else
using StateKey = uint32_t;
inline bool stateKeyValid(StateKey key) {
    return key != 0;
}

inline StateKey makeStateKey(const char* text) {
    StateKey hash = 2166136261u;
    while (text && *text) {
        hash ^= static_cast<uint8_t>(*text++);
        hash *= 16777619u;
    }
    return hash;
}

#endif

struct PageState {
    StateKey key;
    int value;
};

template <size_t Capacity>
class PageStateTable {
   private:
#if NANOUI_DYNAMIC_STORAGE
    std::map<StateKey, int> entries;
#else
    PageState entries[Capacity] = {};
    uint8_t count = 0;
#endif

   public:
    int get(StateKey key, int fallback) const {
        if (!stateKeyValid(key))
            return fallback;
#if NANOUI_DYNAMIC_STORAGE
        auto it = entries.find(key);
        return it == entries.end() ? fallback : it->second;
#else
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].key == key)
                return entries[i].value;
        }
        return fallback;
#endif
    }

    bool set(StateKey key, int value) {
        if (!stateKeyValid(key))
            return false;
#if NANOUI_DYNAMIC_STORAGE
        entries[key] = value;
        return true;
#else
        for (uint8_t i = 0; i < count; ++i) {
            if (entries[i].key == key) {
                entries[i].value = value;
                return true;
            }
        }
        if (count >= Capacity)
            return false;
        entries[count++] = {key, value};
        return true;
#endif
    }
};

}  // namespace NanoUI

#endif
