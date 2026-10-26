#include <NanoUI/foundation/allocator.h>
#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/widget.h>
#include <windows/heap_allocator.h>

#include <cstdint>
#include <iostream>
#include <utility>

namespace {

struct AppState {
    std::uint64_t value = 0;
    std::uint64_t builds = 0;
};

class CounterState final : public NanoUI::State {
public:
    explicit CounterState(AppState& state) noexcept : state_(&state) {}
    NanoUI::BuildResult build(NanoUI::BuildContext&) noexcept override {
        ++state_->builds;
        return {};
    }
private:
    AppState* state_;
};

class CounterWidget final : public NanoUI::StatefulWidget {
public:
    explicit CounterWidget(AppState& state) noexcept
        : StatefulWidget(NanoUI::typeId<CounterWidget>()), state_(&state) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        return NanoUI::makeOwned<CounterState>(allocator, NanoUI::AllocationKind::state, *state_);
    }
private:
    AppState* state_;
};

void increment(NanoUI::State&, void* payload) noexcept {
    ++*static_cast<std::uint64_t*>(payload);
}

} // namespace

int main() {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    AppState state;
    {
        NanoUI::BuildOwner owner(allocator);
        auto root = NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget, state);
        if (owner.setRoot(std::move(root)) != NanoUI::UiError::none) return 1;
        const NanoUI::ElementHandle handle = owner.root();
        for (std::uint64_t i = 0; i < 100000; ++i) {
            if (owner.setState(handle, increment, &state.value) != NanoUI::UiError::none) return 2;
        }
        const NanoUI::PumpResult pump = owner.pump();
        if (pump.error != NanoUI::UiError::none || pump.builds != 1 || pump.needsFrame) return 3;
    }
    const NanoUI::MemoryStatistics& stats = allocator.statistics();
    std::cout << "updates=" << state.value << " builds=" << state.builds
              << " peak_requested_bytes=" << stats.peakBytes
              << " live_blocks_after_teardown=" << stats.currentBlocks() << '\n';
    return state.value == 100000 && state.builds == 2 && stats.currentBlocks() == 0 ? 0 : 4;
}
