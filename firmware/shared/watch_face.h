#pragma once

#include <stdint.h>

namespace Crabit {
// Preserve existing saved IDs; Classic now uses the selected inverted-bar layout.
enum class WatchFace : uint8_t { Classic = 0, Liquid = 1, Minimal = 2, Split = 3, Stacked = 4 };
constexpr uint8_t watchFaceCount = 5;
inline uint8_t validWatchFace(uint8_t value) {
    return value < watchFaceCount ? value : 0;
}
}  // namespace Crabit
