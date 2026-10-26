#pragma once

#include <cstddef>

namespace NanoUI {

enum class AllocationKind : std::size_t {
    widget, element, state, renderObject, container, resource, count
};

// The resource must outlive every allocation returned by it. Single UI context.
class Allocator {
public:
    virtual ~Allocator() = default;
    virtual void* allocate(std::size_t bytes, std::size_t alignment,
                           AllocationKind kind) noexcept = 0;
    virtual void deallocate(void* pointer, std::size_t bytes,
                            std::size_t alignment, AllocationKind kind) noexcept = 0;
};

struct AllocationStatistics {
    std::size_t bytes = 0;
    std::size_t blocks = 0;
};

// Fixed category counters need no STL container or exception/string machinery.
// Keep indexed and range access available on the Arduino SDK's C++11 library.
class AllocationCategories {
public:
    std::size_t size() const noexcept { return categoryCount; }
    AllocationStatistics& operator[](std::size_t index) noexcept { return values_[index]; }
    const AllocationStatistics& operator[](std::size_t index) const noexcept { return values_[index]; }
    AllocationStatistics* begin() noexcept { return values_; }
    AllocationStatistics* end() noexcept { return values_ + categoryCount; }
    const AllocationStatistics* begin() const noexcept { return values_; }
    const AllocationStatistics* end() const noexcept { return values_ + categoryCount; }

private:
    static constexpr std::size_t categoryCount = static_cast<std::size_t>(AllocationKind::count);
    AllocationStatistics values_[categoryCount]{};
};

struct MemoryStatistics {
    AllocationCategories categories{};
    std::size_t peakBytes = 0;
    std::size_t peakBlocks = 0;
    std::size_t failedAllocations = 0;
    std::size_t currentBytes() const noexcept;
    std::size_t currentBlocks() const noexcept;
};

class TrackingAllocator final : public Allocator {
public:
    explicit TrackingAllocator(Allocator& upstream) noexcept : upstream_(upstream) {}
    TrackingAllocator(const TrackingAllocator&) = delete;
    TrackingAllocator& operator=(const TrackingAllocator&) = delete;
    TrackingAllocator(TrackingAllocator&&) = delete;
    TrackingAllocator& operator=(TrackingAllocator&&) = delete;
    void* allocate(std::size_t bytes, std::size_t alignment,
                   AllocationKind kind) noexcept override;
    void deallocate(void* pointer, std::size_t bytes, std::size_t alignment,
                    AllocationKind kind) noexcept override;
    const MemoryStatistics& statistics() const noexcept { return statistics_; }

private:
    Allocator& upstream_;
    MemoryStatistics statistics_;
};

} // namespace NanoUI
