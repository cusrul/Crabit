#include "allocator.h"

#include <limits>

namespace NanoUI {

std::size_t MemoryStatistics::currentBytes() const noexcept {
    std::size_t total = 0;
    for (const auto& category : categories) total += category.bytes;
    return total;
}

std::size_t MemoryStatistics::currentBlocks() const noexcept {
    std::size_t total = 0;
    for (const auto& category : categories) total += category.blocks;
    return total;
}

void* TrackingAllocator::allocate(std::size_t bytes, std::size_t alignment,
                                 AllocationKind kind) noexcept {
    const auto index = static_cast<std::size_t>(kind);
    if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0 ||
        index >= statistics_.categories.size() ||
        bytes > std::numeric_limits<std::size_t>::max() - statistics_.currentBytes() ||
        statistics_.currentBlocks() == std::numeric_limits<std::size_t>::max()) {
        ++statistics_.failedAllocations;
        return nullptr;
    }
    void* pointer = upstream_.allocate(bytes, alignment, kind);
    if (!pointer) {
        ++statistics_.failedAllocations;
        return nullptr;
    }
    auto& category = statistics_.categories[index];
    category.bytes += bytes;
    ++category.blocks;
    const auto bytesNow = statistics_.currentBytes();
    const auto blocksNow = statistics_.currentBlocks();
    if (bytesNow > statistics_.peakBytes) statistics_.peakBytes = bytesNow;
    if (blocksNow > statistics_.peakBlocks) statistics_.peakBlocks = blocksNow;
    return pointer;
}

void TrackingAllocator::deallocate(void* pointer, std::size_t bytes,
                                   std::size_t alignment, AllocationKind kind) noexcept {
    if (!pointer) return;
    // Matching resource, kind, size and alignment are part of the caller contract.
    auto& category = statistics_.categories[static_cast<std::size_t>(kind)];
    category.bytes -= bytes;
    --category.blocks;
    upstream_.deallocate(pointer, bytes, alignment, kind);
}

} // namespace NanoUI
