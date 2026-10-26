#pragma once

#include <NanoUI/foundation/allocator.h>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>

namespace NanoUI::Windows {

// Host backend using CRT heap. Metadata/alignment overhead is not requested bytes.
// Kept separate from the portable core; also usable by headless host tests.
class HeapAllocator final : public Allocator {
public:
    void* allocate(std::size_t bytes, std::size_t alignment, AllocationKind) noexcept override {
        if (bytes == 0 || alignment == 0 || (alignment & (alignment - 1)) != 0) return nullptr;
        if (alignment > std::numeric_limits<std::size_t>::max() - sizeof(void*) ||
            bytes > std::numeric_limits<std::size_t>::max() - sizeof(void*) - (alignment - 1)) return nullptr;
        void* raw = std::malloc(bytes + sizeof(void*) + alignment - 1);
        if (!raw) return nullptr;
        const auto start = reinterpret_cast<std::uintptr_t>(raw) + sizeof(void*);
        const auto aligned = (start + alignment - 1) & ~(static_cast<std::uintptr_t>(alignment) - 1);
        auto* result = reinterpret_cast<unsigned char*>(aligned);
        std::memcpy(result - sizeof(void*), &raw, sizeof(raw));
        return result;
    }
    void deallocate(void* pointer, std::size_t, std::size_t, AllocationKind) noexcept override {
        if (!pointer) return;
        void* raw = nullptr;
        std::memcpy(&raw, static_cast<unsigned char*>(pointer) - sizeof(void*), sizeof(raw));
        std::free(raw);
    }
};

} // namespace NanoUI::Windows
