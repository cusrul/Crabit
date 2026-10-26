#include <NanoUI/navigation/navigator.h>
#include <NanoUI/framework/build_result.h>
#include <windows/heap_allocator.h>
#include <iostream>

namespace {
int failures = 0;
int checks = 0;
#define CHECK(x)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(x)) {                                \
            ++failures;                            \
            std::cerr << __LINE__ << ": " #x "\n"; \
        }                                          \
    } while (false)
class FaultAllocator : public NanoUI::Allocator {
   public:
    void* allocate(std::size_t bytes, std::size_t alignment,
                   NanoUI::AllocationKind kind) noexcept override {
        ++calls;
        return calls == failAt ? nullptr : heap.allocate(bytes, alignment, kind);
    }
    void deallocate(void* p, std::size_t bytes, std::size_t alignment,
                    NanoUI::AllocationKind kind) noexcept override {
        heap.deallocate(p, bytes, alignment, kind);
    }
    NanoUI::Windows::HeapAllocator heap;
    std::size_t calls = 0, failAt = 0;
};
class Page : public NanoUI::LeafWidget {
   public:
    explicit Page(int& destructions) noexcept
        : LeafWidget(NanoUI::typeId<Page>()), destroyed(destructions) {}
    ~Page() noexcept override { ++destroyed; }
    int& destroyed;
};
NanoUI::WidgetPtr page(NanoUI::Allocator& allocator, int& disposed) {
    return NanoUI::makeOwned<Page>(allocator, NanoUI::AllocationKind::widget, disposed);
}
void testNavigation() {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    int disposed = 0;
    {
        NanoUI::Navigator nav(allocator);
        auto first = page(allocator, disposed);
        CHECK(nav.push(first).status == NanoUI::NavigationStatus::queued);
        CHECK(!first && nav.depth() == 0 && nav.pending() == 1);
        CHECK(nav.commit().applied == 1);
        auto* previous = nav.current();
        auto handle = previous->root();
        CHECK(nav.saveFocus(handle) == NanoUI::UiError::none);
        auto second = page(allocator, disposed);
        CHECK(nav.push(second).status == NanoUI::NavigationStatus::queued);
        CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
        CHECK(nav.depth() == 2 && disposed == 0);
        CHECK(previous->inspect(handle).value.phase == NanoUI::ElementPhase::inactive);
        struct Input {
            NanoUI::Navigator& nav;
            int& disposed;
        } input{nav, disposed};
        CHECK(nav.dispatch(
                  [](void* data) noexcept {
                      auto& ctx = *static_cast<Input*>(data);
                      CHECK(ctx.nav.pop().status == NanoUI::NavigationStatus::queued);
                      CHECK(ctx.nav.commit().status == NanoUI::NavigationStatus::busy);
                      CHECK(ctx.nav.depth() == 2 && ctx.disposed == 0);
                  },
                  &input) == NanoUI::UiError::none);
        CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
        CHECK(nav.depth() == 1 && disposed == 1);
        CHECK(nav.current() == previous && nav.focus().serial == handle.serial);
        CHECK(previous->inspect(handle).value.phase == NanoUI::ElementPhase::active);
        CHECK(nav.pop().status == NanoUI::NavigationStatus::queued);
        CHECK(nav.commit().status == NanoUI::NavigationStatus::atRoot);
        CHECK(nav.depth() == 1 && disposed == 1);
        auto discarded = page(allocator, disposed);
        CHECK(nav.push(discarded).status == NanoUI::NavigationStatus::queued);
        CHECK(nav.cancelPending() == NanoUI::UiError::none && nav.pending() == 0);
        CHECK(disposed == 2);
    }
    CHECK(disposed == 3);
    CHECK(allocator.statistics().currentBlocks() == 0);
}
void testFailures() {
    for (std::size_t failOffset = 1; failOffset <= 5; ++failOffset) {
        FaultAllocator fault;
        NanoUI::TrackingAllocator allocator(fault);
        int disposed = 0;
        {
            NanoUI::Navigator nav(allocator, {1, 1, {}});
            auto first = page(allocator, disposed);
            CHECK(nav.push(first).status == NanoUI::NavigationStatus::queued);
            CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
            auto original = nav.current()->root();
            auto second = page(allocator, disposed);
            CHECK(nav.push(second).status == NanoUI::NavigationStatus::queued);
            auto third = page(allocator, disposed);
            CHECK(nav.push(third).error == NanoUI::UiError::capacityExceeded && third);
            CHECK(nav.commit().error == NanoUI::UiError::capacityExceeded);
            CHECK(nav.current()->root().serial == original.serial);
            CHECK(nav.cancelPending() == NanoUI::UiError::none);
        }
        CHECK(allocator.statistics().currentBlocks() == 0);
        // A fresh Navigator lets the staged route exercise every allocation.
        {
            NanoUI::Navigator nav(allocator);
            auto first = page(allocator, disposed);
            CHECK(nav.push(first).status == NanoUI::NavigationStatus::queued);
            CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
            auto* old = nav.current();
            auto second = page(allocator, disposed);
            fault.failAt = fault.calls + failOffset;
            const auto queued = nav.push(second);
            if (queued.status == NanoUI::NavigationStatus::failed)
                CHECK(second);
            else {
                CHECK(!second);
                const auto result = nav.commit();
                if (result.status == NanoUI::NavigationStatus::failed) {
                    CHECK(nav.depth() == 1 && nav.current() == old);
                    CHECK(old->inspect(old->root()).value.phase == NanoUI::ElementPhase::active);
                } else
                    CHECK(nav.depth() == 2);
            }
            fault.failAt = 0;
        }
        CHECK(allocator.statistics().currentBlocks() == 0);
    }
}
}  // namespace
int main() {
    testNavigation();
    testFailures();
    std::cout << "Navigation: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
