#include <NanoUI/foundation/allocator.h>
#include <NanoUI/foundation/owned.h>
#include <NanoUI/foundation/owned_text.h>
#include <windows/heap_allocator.h>

#include <chrono>
#include <climits>
#include <cstdint>
#include <iostream>

namespace {
struct alignas(64) Sample {
    explicit Sample(std::uint32_t number) noexcept : value(number) {}
    std::uint32_t value;
};
}

int main() {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator tracked(heap);
    constexpr std::uint32_t cycles = 100000;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint32_t i = 0; i < cycles; ++i) {
        auto sample = NanoUI::makeOwned<Sample>(tracked, NanoUI::AllocationKind::widget, i);
        NanoUI::OwnedText label(tracked);
        if (!sample || label.assign("NanoUI owns temporary text") != NanoUI::TextResult::ok) {
            std::cerr << "Allocation failed; this run is not accepted\n";
            return 1;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    const auto& usage = tracked.statistics();
    std::cout << "Foundation allocation workload; NOT a UI frame benchmark\n"
              << "pointer_bits=" << sizeof(void*) * CHAR_BIT << '\n'
              << "cycles=" << cycles << " elapsed_us=" << elapsed << '\n'
              << "sizeof_owned_sample=" << sizeof(NanoUI::Owned<Sample>)
              << " sizeof_owned_text=" << sizeof(NanoUI::OwnedText) << '\n'
              << "peak_requested_bytes=" << usage.peakBytes
              << " peak_blocks=" << usage.peakBlocks << '\n'
              << "final_requested_bytes=" << usage.currentBytes()
              << " final_blocks=" << usage.currentBlocks()
              << " failed_allocations=" << usage.failedAllocations << '\n'
              << "Excluded: CRT metadata, process RSS, embedded RAM/Flash, layout/paint\n";
    return usage.currentBytes() == 0 && usage.currentBlocks() == 0 && usage.failedAllocations == 0 ? 0 : 1;
}
