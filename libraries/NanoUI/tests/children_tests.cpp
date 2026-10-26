#include <NanoUI/foundation/allocator.h>
#include <NanoUI/foundation/owned.h>
#include <NanoUI/framework/build_context.h>
#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/state.h>
#include <NanoUI/framework/widget.h>
#include <NanoUI/framework/widget_list.h>
#include <windows/heap_allocator.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(condition)                                                                    \
    do {                                                                                    \
        ++checks;                                                                           \
        if (!(condition)) {                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #condition "\n"; \
            ++failures;                                                                     \
        }                                                                                   \
    } while (false)

class FaultAllocator final : public NanoUI::Allocator {
   public:
    struct Record {
        void* pointer = nullptr;
        std::size_t bytes = 0;
        std::size_t alignment = 0;
        NanoUI::AllocationKind kind = NanoUI::AllocationKind::resource;
        bool live = false;
    };

    void* allocate(std::size_t bytes, std::size_t alignment,
                   NanoUI::AllocationKind kind) noexcept override {
        ++calls;
        if (failAt != 0 && calls == failAt)
            return nullptr;
        void* pointer = heap.allocate(bytes, alignment, kind);
        if (!pointer)
            return nullptr;
        for (auto& record : records) {
            if (!record.live) {
                record = Record{pointer, bytes, alignment, kind, true};
                ++allocations;
                return pointer;
            }
        }
        ++recordOverflow;
        return pointer;
    }

    void deallocate(void* pointer, std::size_t bytes, std::size_t alignment,
                    NanoUI::AllocationKind kind) noexcept override {
        for (auto& record : records) {
            if (record.live && record.pointer == pointer) {
                if (record.bytes != bytes || record.alignment != alignment || record.kind != kind)
                    ++parameterMismatches;
                record.live = false;
                ++deallocations;
                heap.deallocate(pointer, bytes, alignment, kind);
                return;
            }
        }
        ++unknownDeallocations;
        heap.deallocate(pointer, bytes, alignment, kind);
    }

    NanoUI::Windows::HeapAllocator heap;
    Record records[256]{};
    std::size_t calls = 0;
    std::size_t failAt = 0;
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    std::size_t parameterMismatches = 0;
    std::size_t unknownDeallocations = 0;
    std::size_t recordOverflow = 0;
};

bool balanced(const FaultAllocator& allocator) {
    CHECK(allocator.parameterMismatches == 0);
    CHECK(allocator.unknownDeallocations == 0);
    CHECK(allocator.recordOverflow == 0);
    CHECK(allocator.allocations == allocator.deallocations);
    for (const auto& record : allocator.records)
        CHECK(!record.live);
    return failures == 0;
}

struct ChildStats {
    int initialized = 0;
    int disposed = 0;
    int updates = 0;
    int builds = 0;
};

struct Plan {
    enum class Shape { list, single, empty, both } shape = Shape::list;
    int ids[8]{};
    bool keyed = true;
    std::size_t count = 0;
    ChildStats children[8]{};
};

class ChildState final : public NanoUI::State {
   public:
    ChildState(ChildStats& stats, int id) noexcept : stats_(&stats), id_(id) {}
    void initState(NanoUI::BuildContext&) noexcept override { ++stats_->initialized; }
    void didUpdateWidget(const NanoUI::Widget& previous, NanoUI::BuildContext&) noexcept override;
    NanoUI::BuildResult build(NanoUI::BuildContext&) noexcept override {
        ++stats_->builds;
        return {};
    }
    void dispose() noexcept override { ++stats_->disposed; }

   private:
    ChildStats* stats_;
    int id_;
};

class ChildWidget final : public NanoUI::StatefulWidget {
   public:
    ChildWidget(ChildStats& stats, int id, bool keyed) noexcept
        : StatefulWidget(
              NanoUI::typeId<ChildWidget>(),
              keyed ? NanoUI::Key::value(static_cast<std::uint64_t>(id)) : NanoUI::Key{}),
          stats_(&stats),
          id_(id) {}

    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        return NanoUI::makeOwned<ChildState>(allocator, NanoUI::AllocationKind::state, *stats_,
                                             id_);
    }

   private:
    ChildStats* stats_;
    int id_;
};

void ChildState::didUpdateWidget(const NanoUI::Widget& previous, NanoUI::BuildContext&) noexcept {
    const auto& old = static_cast<const ChildWidget&>(previous);
    (void)old;
    ++stats_->updates;
}

class ParentState final : public NanoUI::State {
   public:
    explicit ParentState(Plan& plan) noexcept : plan_(&plan) {}
    NanoUI::BuildResult build(NanoUI::BuildContext& context) noexcept override {
        if (plan_->shape == Plan::Shape::empty)
            return {};
        if (plan_->shape == Plan::Shape::single) {
            auto child = makeChild(context.allocator(), plan_->ids[0]);
            if (!child)
                return {NanoUI::UiError::outOfMemory, {}};
            return {NanoUI::UiError::none, std::move(child)};
        }

        NanoUI::BuildResult result;
        if (plan_->shape == Plan::Shape::both) {
            result.child = makeChild(context.allocator(), plan_->ids[0]);
            if (!result.child)
                return {NanoUI::UiError::outOfMemory, {}};
        }
        for (std::size_t i = 0; i < plan_->count; ++i) {
            auto child = makeChild(context.allocator(), plan_->ids[i]);
            if (!child)
                return {NanoUI::UiError::outOfMemory, {}};
            const auto appended = result.children.append(context.allocator(), child);
            if (appended != NanoUI::UiError::none)
                return {appended, {}};
            CHECK(!child);
        }
        return result;
    }

   private:
    NanoUI::WidgetPtr makeChild(NanoUI::Allocator& allocator, int id) noexcept {
        const std::size_t slot = static_cast<std::size_t>(id);
        if (slot >= 8)
            return {};
        return NanoUI::makeOwned<ChildWidget>(allocator, NanoUI::AllocationKind::widget,
                                              plan_->children[slot], id, plan_->keyed);
    }
    Plan* plan_;
};

class ParentWidget final : public NanoUI::StatefulWidget {
   public:
    explicit ParentWidget(Plan& plan) noexcept
        : StatefulWidget(NanoUI::typeId<ParentWidget>()), plan_(&plan) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        return NanoUI::makeOwned<ParentState>(allocator, NanoUI::AllocationKind::state, *plan_);
    }

   private:
    Plan* plan_;
};

NanoUI::UiError setPlan(NanoUI::BuildOwner& owner, Plan& plan, Plan::Shape shape, const int* ids,
                        std::size_t count, bool keyed = true) {
    plan.shape = shape;
    plan.keyed = keyed;
    plan.count = count;
    for (std::size_t i = 0; i < count; ++i)
        plan.ids[i] = ids[i];
    return owner.requestBuild(owner.root()) == NanoUI::UiError::none
               ? owner.pump().error
               : NanoUI::UiError::invalidPhase;
}

NanoUI::UiError installRoot(NanoUI::BuildOwner& owner, NanoUI::Allocator& allocator, Plan& plan) {
    auto root = NanoUI::makeOwned<ParentWidget>(allocator, NanoUI::AllocationKind::widget, plan);
    if (!root)
        return NanoUI::UiError::outOfMemory;
    return owner.setRoot(std::move(root));
}

bool mountList(NanoUI::BuildOwner& owner, NanoUI::Allocator& allocator, Plan& plan, const int* ids,
               std::size_t count, bool keyed = true) {
    CHECK(installRoot(owner, allocator, plan) == NanoUI::UiError::none);
    const auto root = owner.root();
    plan.keyed = keyed;
    plan.count = count;
    for (std::size_t i = 0; i < count; ++i)
        plan.ids[i] = ids[i];
    CHECK(owner.requestBuild(root) == NanoUI::UiError::none);
    const auto result = owner.pump();
    CHECK(result.error == NanoUI::UiError::none);
    CHECK(owner.inspect(root).value.childCount == count);
    return result.error == NanoUI::UiError::none;
}

void testWidgetListAppendOwnershipOnSuccessAndFailure() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    NanoUI::WidgetList list;
    NanoUI::WidgetPtr widget = NanoUI::makeOwned<ChildWidget>(
        allocator, NanoUI::AllocationKind::widget, plan.children[1], 1, true);
    CHECK(widget);

    upstream.failAt = upstream.calls + 1;
    CHECK(list.append(allocator, widget) == NanoUI::UiError::outOfMemory);
    CHECK(widget);
    CHECK(list.empty());

    upstream.failAt = 0;
    CHECK(list.append(allocator, widget) == NanoUI::UiError::none);
    CHECK(!widget);
    CHECK(!list.empty());
    CHECK(list.first() && list.first()->widget);
    list = NanoUI::WidgetList{};
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testKeyedReorderAndAddRemove() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    const int initial[] = {1, 2, 3};
    const int reordered[] = {3, 1, 4};
    {
        NanoUI::BuildOwner owner(allocator);
        CHECK(mountList(owner, allocator, plan, initial, 3));
        const auto root = owner.root();
        const auto one = owner.childAt(root, 0);
        const auto two = owner.childAt(root, 1);
        const auto three = owner.childAt(root, 2);
        CHECK(plan.children[1].initialized == 1);
        CHECK(plan.children[2].initialized == 1);
        CHECK(plan.children[3].initialized == 1);

        CHECK(setPlan(owner, plan, Plan::Shape::list, reordered, 3) == NanoUI::UiError::none);
        CHECK(owner.inspect(root).value.childCount == 3);
        CHECK(owner.childAt(root, 0).serial == three.serial);
        CHECK(owner.childAt(root, 1).serial == one.serial);
        CHECK(owner.childAt(root, 2).serial != two.serial);
        CHECK(owner.inspect(one).error == NanoUI::UiError::none);
        CHECK(owner.inspect(three).error == NanoUI::UiError::none);
        CHECK(owner.inspect(two).error == NanoUI::UiError::staleHandle);
        CHECK(plan.children[1].updates == 1);
        CHECK(plan.children[3].updates == 1);
        CHECK(plan.children[2].disposed == 1);
        CHECK(plan.children[4].initialized == 1);

        CHECK(owner.clearRoot() == NanoUI::UiError::none);
        CHECK(owner.inspect(one).error == NanoUI::UiError::staleHandle);
        CHECK(owner.inspect(three).error == NanoUI::UiError::staleHandle);
        CHECK(plan.children[1].disposed == 1);
        CHECK(plan.children[3].disposed == 1);
        CHECK(plan.children[4].disposed == 1);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testUnkeyedOnlyMatchesByPosition() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    const int initial[] = {1, 2};
    const int changed[] = {3, 4};
    {
        NanoUI::BuildOwner owner(allocator);
        CHECK(mountList(owner, allocator, plan, initial, 2, false));
        const auto root = owner.root();
        const auto first = owner.childAt(root, 0);
        const auto second = owner.childAt(root, 1);
        CHECK(setPlan(owner, plan, Plan::Shape::list, changed, 2, false) == NanoUI::UiError::none);
        CHECK(owner.childAt(root, 0).serial == first.serial);
        CHECK(owner.childAt(root, 1).serial == second.serial);
        CHECK(owner.inspect(first).error == NanoUI::UiError::none);
        CHECK(owner.inspect(second).error == NanoUI::UiError::none);
        CHECK(plan.children[1].updates == 1);
        CHECK(plan.children[2].updates == 1);
        CHECK(plan.children[3].initialized == 0 && plan.children[4].initialized == 0);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
        CHECK(plan.children[1].disposed == 1 && plan.children[2].disposed == 1);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testDuplicateKeysAndMixedChildFormsAreRejectedAtomically() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    const int initial[] = {1, 2, 3};
    const int duplicates[] = {4, 4};
    const int one[] = {5};
    {
        NanoUI::BuildOwner owner(allocator);
        CHECK(mountList(owner, allocator, plan, initial, 3));
        const auto root = owner.root();
        const NanoUI::ElementHandle original[3] = {owner.childAt(root, 0), owner.childAt(root, 1),
                                                   owner.childAt(root, 2)};

        plan.shape = Plan::Shape::list;
        plan.count = 2;
        plan.keyed = true;
        plan.ids[0] = duplicates[0];
        plan.ids[1] = duplicates[1];
        CHECK(owner.requestBuild(root) == NanoUI::UiError::none);
        CHECK(owner.pump().error == NanoUI::UiError::invalidInput);
        CHECK(owner.inspect(root).value.childCount == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(owner.childAt(root, i).serial == original[i].serial);
            CHECK(owner.inspect(original[i]).error == NanoUI::UiError::none);
        }
        CHECK(plan.children[1].disposed == 0 && plan.children[2].disposed == 0 &&
              plan.children[3].disposed == 0);

        plan.shape = Plan::Shape::both;
        plan.count = 1;
        plan.ids[0] = one[0];
        CHECK(owner.requestBuild(root) == NanoUI::UiError::none);
        CHECK(owner.pump().error == NanoUI::UiError::invalidInput);
        CHECK(owner.inspect(root).value.childCount == 3);
        for (std::size_t i = 0; i < 3; ++i) {
            CHECK(owner.childAt(root, i).serial == original[i].serial);
            CHECK(owner.inspect(original[i]).error == NanoUI::UiError::none);
        }
        CHECK(plan.children[1].disposed == 0 && plan.children[2].disposed == 0 &&
              plan.children[3].disposed == 0);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testMultiToSingleToEmptyDisposesAndInvalidates() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    const int initial[] = {1, 2, 3};
    const int only[] = {4};
    {
        NanoUI::BuildOwner owner(allocator);
        CHECK(mountList(owner, allocator, plan, initial, 3));
        const auto root = owner.root();
        const NanoUI::ElementHandle old[3] = {owner.childAt(root, 0), owner.childAt(root, 1),
                                              owner.childAt(root, 2)};
        CHECK(setPlan(owner, plan, Plan::Shape::single, only, 1) == NanoUI::UiError::none);
        CHECK(owner.inspect(root).value.childCount == 1);
        const auto single = owner.childAt(root, 0);
        for (const auto handle : old)
            CHECK(owner.inspect(handle).error == NanoUI::UiError::staleHandle);
        CHECK(plan.children[1].disposed == 1 && plan.children[2].disposed == 1 &&
              plan.children[3].disposed == 1);
        CHECK(plan.children[4].initialized == 1);

        CHECK(setPlan(owner, plan, Plan::Shape::empty, nullptr, 0) == NanoUI::UiError::none);
        CHECK(owner.inspect(root).value.childCount == 0);
        CHECK(!owner.childAt(root, 0).owner);
        CHECK(owner.inspect(single).error == NanoUI::UiError::staleHandle);
        CHECK(plan.children[4].disposed == 1);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testInactiveRootAndBudgetContinuation() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    const int initial[] = {1, 2, 3};
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions{8, 2, 8});
        // Atomic staging cannot mount root + three new States with a two-build
        // budget. Grow one new child per operation (root + new child = two),
        // then drain compatible updates before testing inactive/resume pumping.
        for (std::size_t count = 1; count <= 3; ++count)
            CHECK(mountList(owner, allocator, plan, initial, count));
        NanoUI::PumpResult settled;
        for (int frame = 0; frame < 4; ++frame) {
            settled = owner.pump();
            CHECK(settled.error == NanoUI::UiError::none);
            if (!settled.needsFrame)
                break;
        }
        CHECK(!settled.needsFrame);
        const auto root = owner.root();
        const auto first = owner.childAt(root, 0);
        const auto second = owner.childAt(root, 1);
        const auto third = owner.childAt(root, 2);

        CHECK(owner.setRootActive(false) == NanoUI::UiError::none);
        CHECK(owner.requestBuild(first) == NanoUI::UiError::none);
        CHECK(owner.requestBuild(second) == NanoUI::UiError::none);
        CHECK(owner.requestBuild(third) == NanoUI::UiError::none);
        const auto inactive = owner.pump();
        CHECK(inactive.error == NanoUI::UiError::none);
        CHECK(inactive.builds == 0 && !inactive.needsFrame);
        CHECK(owner.inspect(first).value.dirty && owner.inspect(second).value.dirty &&
              owner.inspect(third).value.dirty);

        CHECK(owner.setRootActive(true) == NanoUI::UiError::none);
        const auto firstFrame = owner.pump();
        CHECK(firstFrame.error == NanoUI::UiError::none);
        CHECK(firstFrame.builds == 2 && firstFrame.needsFrame);
        CHECK(!owner.inspect(first).value.dirty && !owner.inspect(second).value.dirty);
        CHECK(owner.inspect(third).value.dirty);
        const auto resumed = owner.pump();
        CHECK(resumed.error == NanoUI::UiError::none);
        CHECK(resumed.builds == 1 && !resumed.needsFrame);
        CHECK(!owner.inspect(third).value.dirty);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testAllocationFailureSweepPreservesOldOrderAndRetries() {
    bool reachedSuccess = false;
    std::size_t failedOffsets = 0;
    const int oldIds[] = {1, 2, 3};
    const int newIds[] = {4, 3, 1};  // Stage a new child before two keyed matches.

    for (std::size_t offset = 1; offset <= 20 && !reachedSuccess; ++offset) {
        FaultAllocator upstream;
        NanoUI::TrackingAllocator allocator(upstream);
        Plan plan;
        {
            NanoUI::BuildOwner owner(allocator);
            CHECK(mountList(owner, allocator, plan, oldIds, 3));
            const auto root = owner.root();
            const NanoUI::ElementHandle old[3] = {owner.childAt(root, 0), owner.childAt(root, 1),
                                                  owner.childAt(root, 2)};
            const auto oldBlocks = allocator.statistics().currentBlocks();
            plan.shape = Plan::Shape::list;
            plan.keyed = true;
            plan.count = 3;
            for (std::size_t i = 0; i < 3; ++i)
                plan.ids[i] = newIds[i];
            CHECK(owner.requestBuild(root) == NanoUI::UiError::none);
            upstream.failAt = upstream.calls + offset;
            const auto attempt = owner.pump();
            upstream.failAt = 0;

            if (attempt.error == NanoUI::UiError::none) {
                reachedSuccess = true;
                CHECK(owner.inspect(root).value.childCount == 3);
                CHECK(owner.childAt(root, 0).serial != old[0].serial);
                CHECK(owner.childAt(root, 1).serial == old[2].serial);
                CHECK(owner.childAt(root, 2).serial == old[0].serial);
                CHECK(owner.inspect(old[1]).error == NanoUI::UiError::staleHandle);
                CHECK(owner.inspect(old[0]).error == NanoUI::UiError::none);
                CHECK(owner.inspect(old[2]).error == NanoUI::UiError::none);
                CHECK(plan.children[4].initialized == 1 && plan.children[4].disposed == 0);
                CHECK(plan.children[2].disposed == 1);
                CHECK(plan.children[1].updates == 1 && plan.children[3].updates == 1);
                CHECK(owner.clearRoot() == NanoUI::UiError::none);
            } else {
                ++failedOffsets;
                CHECK(attempt.error == NanoUI::UiError::outOfMemory);
                CHECK(owner.inspect(root).value.childCount == 3);
                CHECK(allocator.statistics().currentBlocks() == oldBlocks);
                for (std::size_t i = 0; i < 3; ++i) {
                    CHECK(owner.childAt(root, i).serial == old[i].serial);
                    CHECK(owner.inspect(old[i]).error == NanoUI::UiError::none);
                }
                CHECK(plan.children[1].disposed == 0 && plan.children[2].disposed == 0 &&
                      plan.children[3].disposed == 0);
                CHECK(plan.children[1].updates == 0 && plan.children[3].updates == 0);
                CHECK(plan.children[4].disposed == plan.children[4].initialized);
                CHECK(owner.inspect(root).value.dirty);
                upstream.failAt = 0;
                const auto retry = owner.pump();
                CHECK(retry.error == NanoUI::UiError::none);
                CHECK(owner.childAt(root, 0).serial != old[0].serial);
                CHECK(owner.childAt(root, 1).serial == old[2].serial);
                CHECK(owner.childAt(root, 2).serial == old[0].serial);
                CHECK(owner.inspect(old[1]).error == NanoUI::UiError::staleHandle);
                CHECK(owner.clearRoot() == NanoUI::UiError::none);
            }
        }
        CHECK(allocator.statistics().currentBlocks() == 0);
        balanced(upstream);
    }
    CHECK(reachedSuccess);
    CHECK(failedOffsets >= 8);  // Includes failure after the new child was staged.
}

void testExplicitChildCapacity() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Plan plan;
    const int ids[] = {1, 2, 3};
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions{8, 32, 0});
        CHECK(installRoot(owner, allocator, plan) == NanoUI::UiError::none);
        CHECK(setPlan(owner, plan, Plan::Shape::single, ids, 1) ==
              NanoUI::UiError::capacityExceeded);
        CHECK(owner.inspect(owner.root()).value.childCount == 0);
    }
    {
        plan.shape = Plan::Shape::empty;
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions{8, 32, 2});
        CHECK(installRoot(owner, allocator, plan) == NanoUI::UiError::none);
        CHECK(setPlan(owner, plan, Plan::Shape::list, ids, 3) == NanoUI::UiError::capacityExceeded);
        CHECK(owner.inspect(owner.root()).value.childCount == 0);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

}  // namespace

int main() {
    testWidgetListAppendOwnershipOnSuccessAndFailure();
    testKeyedReorderAndAddRemove();
    testUnkeyedOnlyMatchesByPosition();
    testDuplicateKeysAndMixedChildFormsAreRejectedAtomically();
    testMultiToSingleToEmptyDisposesAndInvalidates();
    testInactiveRootAndBudgetContinuation();
    testAllocationFailureSweepPreservesOldOrderAndRetries();
    testExplicitChildCapacity();
    if (failures != 0)
        std::cerr << failures << " children check(s) failed\n";
    else
        std::cout << "NanoUI children: 8 cases / " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
