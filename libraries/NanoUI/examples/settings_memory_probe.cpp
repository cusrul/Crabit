#include <hal/font_engine.h>
#include <core/default_font.h>
#include <NanoUI/ports/legacy_bitmap_font.h>
#include <NanoUI/framework/build_owner.h>
#include <NanoUI/ports/pixel_canvas.h>
#include <pages/settings_hub.h>
#include <pages/settings_hub_route.h>
#include <windows/heap_allocator.h>
#include <windows/raster_canvas.h>

#include <cstdio>
#include <cstdlib>

namespace {
using namespace NanoUI;

class InstrumentAllocator final : public Allocator {
public:
    explicit InstrumentAllocator(Allocator& upstream) noexcept : upstream_(upstream) {}
    void* allocate(std::size_t bytes, std::size_t alignment, AllocationKind kind) noexcept override {
        ++allocCalls;
        void* p = upstream_.allocate(bytes, alignment, kind);
        if (p) {
            liveBytes += bytes;
            const auto index = static_cast<std::size_t>(kind);
            categoryBytes[index] += bytes;
            ++categoryBlocks[index];
            if (liveBytes > peakBytes) {
                // First snapshot at the global byte peak. The maximum block
                // count may occur at another time and must not be conflated.
                peakBytes = liveBytes;
                for (std::size_t i = 0; i < categoryBytesCount; ++i) {
                    peakCategoryBytes[i] = categoryBytes[i];
                    peakCategoryBlocks[i] = categoryBlocks[i];
                }
            }
        }
        return p;
    }
    void deallocate(void* p, std::size_t bytes, std::size_t alignment, AllocationKind kind) noexcept override {
        ++freeCalls;
        if (p) {
            liveBytes -= bytes;
            const auto index = static_cast<std::size_t>(kind);
            categoryBytes[index] -= bytes;
            --categoryBlocks[index];
        }
        upstream_.deallocate(p, bytes, alignment, kind);
    }
    std::size_t allocCalls = 0, freeCalls = 0;
    static constexpr std::size_t categoryBytesCount = static_cast<std::size_t>(AllocationKind::count);
    std::size_t liveBytes = 0, peakBytes = 0;
    std::size_t categoryBytes[categoryBytesCount]{};
    std::size_t categoryBlocks[categoryBytesCount]{};
    std::size_t peakCategoryBytes[categoryBytesCount]{};
    std::size_t peakCategoryBlocks[categoryBytesCount]{};
private:
    Allocator& upstream_;
};

bool injectedAction(CrabitUI::SettingsDestination, void*) noexcept { return true; }

const char* const names[] = {"widget", "element", "state", "renderObject", "container", "resource"};
bool ok = true;
void require(bool condition, const char* what) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", what); ok = false; }
}
void report(const char* phase, const TrackingAllocator& tracker,
            const InstrumentAllocator& instrument, bool showPeakSnapshot = false) {
    const auto& s = tracker.statistics();
    std::printf("phase=%s live_bytes=%zu live_blocks=%zu peak_bytes=%zu peak_blocks=%zu upstream_alloc_calls=%zu\n",
        phase, s.currentBytes(), s.currentBlocks(), s.peakBytes, s.peakBlocks, instrument.allocCalls);
    for (std::size_t i = 0; i < static_cast<std::size_t>(AllocationKind::count); ++i) {
        const auto& c = s.categories[i];
        std::printf("  kind=%s live_bytes=%zu live_blocks=%zu", names[i], c.bytes, c.blocks);
        if (showPeakSnapshot) std::printf(" global_peak_snapshot_bytes=%zu global_peak_snapshot_blocks=%zu",
            instrument.peakCategoryBytes[i], instrument.peakCategoryBlocks[i]);
        std::putchar('\n');
    }
}
}

namespace {
void exercise(BuildOwner& owner, RasterCanvas& canvas, TrackingAllocator& tracker, InstrumentAllocator& instrument) {
    const Size viewport(128, 64);
    const auto first = owner.frame(canvas, viewport);
    require(first.error == UiError::none && first.painted, "mounted first frame");
    report("mounted+firstframe", tracker, instrument);
    require(owner.focusNext() == UiError::none, "first focusNext");
    require(owner.frame(canvas, viewport).error == UiError::none, "first focus frame");
    report("firstfocus+frame", tracker, instrument);
    const auto steadyBytes = tracker.statistics().currentBytes();
    const auto steadyBlocks = tracker.statistics().currentBlocks();
    for (int i = 0; i < 1000; ++i) {
        require(owner.focusNext() == UiError::none, "focus cycle");
        const auto selected = owner.inspect(owner.focus());
        require(selected.error == UiError::none &&
            selected.value.key == CrabitUI::SettingsHubModel::rowKey(static_cast<std::size_t>((i + 1) % 5)),
            "five entries cycle in order");
        require(owner.frame(canvas, viewport).error == UiError::none, "focus cycle frame");
        require(tracker.statistics().currentBytes() == steadyBytes && tracker.statistics().currentBlocks() == steadyBlocks,
            "live requested allocation steady across focus cycles");
    }
    report("1000_focusNext/frame_cycles", tracker, instrument);
    const auto callsBeforeClean = instrument.allocCalls;
    const auto clean = owner.frame(canvas, viewport);
    require(clean.error == UiError::none && !clean.painted, "clean frame does not paint");
    const auto cleanAllocations = instrument.allocCalls - callsBeforeClean;
    std::printf("cleanframe_allocation_call_delta=%zu\n", cleanAllocations);
    require(cleanAllocations == 0, "clean frame has zero upstream allocation calls");
    require(canvas.balanced() && !canvas.error(), "balanced valid raster operations");
    report("cleanframe", tracker, instrument, true);
    require(owner.clearRoot() == UiError::none, "clear tree");
    report("cleartree", tracker, instrument, true);
}

void scenario(bool routeOwned) {
    std::printf("platform=%s scenario=%s\n", NANOUI_LEGACY_NAME,
        routeOwned ? "state_owned_page_without_navigator" : "external_page_model");
    Windows::HeapAllocator heap;
    InstrumentAllocator instrument(heap);
    TrackingAllocator tracker(instrument);
    RasterCanvas canvas(128, 64);
    LegacyBitmapFont font(font5x7, get_chinese_glyph);
    require(!canvas.error(), "RasterCanvas construction");

    std::printf("sizeof_SettingsHubModel=%zu sizeof_BuildOwner=%zu sizeof_RasterCanvas=%zu sizeof_PixelCanvas=%zu\n",
        sizeof(CrabitUI::SettingsHubModel), sizeof(BuildOwner), sizeof(RasterCanvas), sizeof(PixelCanvas));
    std::puts("note=requested allocator bytes only; excludes RasterCanvas pixel buffer, allocator bookkeeping/heap overhead, and process RSS");
    std::puts(routeOwned ? "model_accounting=included via State allocator" : "model_accounting=excluded external host-owned object");

    {
        BuildOwner owner(tracker, OwnerOptions(63, 512, 64));
        report("emptyowner", tracker, instrument);
        if (routeOwned) {
            require(owner.setRoot(makeOwned<CrabitUI::SettingsHubRoute>(tracker,
                AllocationKind::widget, font, 64, injectedAction, nullptr)) == UiError::none, "mount owned Settings session");
            exercise(owner, canvas, tracker, instrument);
        } else {
            CrabitUI::SettingsHubModel model(owner, font, 64, injectedAction, nullptr);
            require(owner.setRoot(makeOwned<CrabitUI::SettingsHub>(tracker,
                AllocationKind::widget, model)) == UiError::none, "mount external-model Settings");
            exercise(owner, canvas, tracker, instrument); // Unmount before model dies.
        }
    }
    report("destroyedowner", tracker, instrument, true);
    require(tracker.statistics().currentBytes() == 0 && tracker.statistics().currentBlocks() == 0,
        "no tracked residual allocations after owner destruction");
    require(instrument.allocCalls == instrument.freeCalls, "upstream allocation/free calls balanced");
    std::size_t peakSum = 0;
    std::size_t peakSnapshotBlocks = 0;
    for (const auto bytes : instrument.peakCategoryBytes) peakSum += bytes;
    for (const auto blocks : instrument.peakCategoryBlocks) peakSnapshotBlocks += blocks;
    std::printf("global_byte_peak_snapshot_bytes=%zu snapshot_blocks=%zu maximum_live_blocks=%zu\n",
        peakSum, peakSnapshotBlocks, tracker.statistics().peakBlocks);
    require(peakSum == tracker.statistics().peakBytes && instrument.peakBytes == tracker.statistics().peakBytes,
        "category snapshot captured at the same global requested-byte peak");
}
} // namespace

int main() {
    scenario(false);
    scenario(true);
    return ok ? 0 : 1;
}
