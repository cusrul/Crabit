#pragma once
#include <cstddef>
inline size_t xPortGetFreeHeapSize() {
    return 12 * 1024;
}
constexpr size_t configTOTAL_HEAP_SIZE = 24 * 1024;
inline size_t xPortGetMinimumEverFreeHeapSize() {
    return 10 * 1024;
}
