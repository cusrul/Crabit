#include <NanoUI/foundation/allocator.h>
#include <NanoUI/foundation/owned.h>
#include <NanoUI/foundation/owned_text.h>
#include <NanoUI/framework/build_context.h>
#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/widget.h>
#include <windows/heap_allocator.h>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <type_traits>
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
    void* allocate(std::size_t bytes, std::size_t alignment,
                   NanoUI::AllocationKind kind) noexcept override {
        ++calls;
        if (failAt != 0 && calls == failAt)
            return nullptr;
        void* pointer = heap.allocate(bytes, alignment, kind);
        if (pointer) {
            for (auto& record : records) {
                if (!record.live) {
                    record = Record{pointer, bytes, alignment, kind, true};
                    ++allocations;
                    return pointer;
                }
            }
            ++recordOverflow;
        }
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

    struct Record {
        void* pointer = nullptr;
        std::size_t bytes = 0;
        std::size_t alignment = 0;
        NanoUI::AllocationKind kind = NanoUI::AllocationKind::resource;
        bool live = false;
    };
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

struct Metrics {
    NanoUI::BuildOwner* owner = nullptr;
    int init = 0;
    int dispose = 0;
    int update = 0;
    int builds = 0;
    int mutations = 0;
    int value = 0;
    int oldConfigAliveInHook = 0;
    int createStateReentry = 0;
    int mutationReentry = 0;
    int updateReentry = 0;
    int updateRequestReentry = 0;
    int initReentry = 0;
    int buildReentry = 0;
    int disposeReentry = 0;
    int stateDestructorReentry = 0;
    int widgetDestructorReentry = 0;
    int mutationCallbackCalls = 0;
    int createdHandleObservations = 0;
    int stagedOldRootObservations = 0;
    int configDestructions = 0;
    std::uint64_t expectedOldRootSerial = 0;
    std::string_view expectedPreviousText{};
    bool enableCreateStateProbe = true;
    bool emitChild = false;
    bool emitSecondLeaf = false;
};

void shouldNotRunMutation(NanoUI::State&, void* payload) noexcept {
    ++*static_cast<int*>(payload);
}

class LeafOne final : public NanoUI::LeafWidget {
   public:
    explicit LeafOne(NanoUI::Key key = {}) noexcept : LeafWidget(NanoUI::typeId<LeafOne>(), key) {}
};
class LeafTwo final : public NanoUI::LeafWidget {
   public:
    explicit LeafTwo(NanoUI::Key key = {}) noexcept : LeafWidget(NanoUI::typeId<LeafTwo>(), key) {}
};

struct DestructorReentry {
    NanoUI::BuildOwner* owner = nullptr;
    NanoUI::UiError result = NanoUI::UiError::none;
    int calls = 0;
};

class ReentrantLeaf final : public NanoUI::LeafWidget {
   public:
    explicit ReentrantLeaf(DestructorReentry& reentry) noexcept
        : LeafWidget(NanoUI::typeId<ReentrantLeaf>(), NanoUI::Key::value(2)), reentry_(&reentry) {}
    ~ReentrantLeaf() noexcept override {
        ++reentry_->calls;
        reentry_->result = reentry_->owner->clearRoot();
    }

   private:
    DestructorReentry* reentry_;
};

class CounterState final : public NanoUI::State {
   public:
    explicit CounterState(Metrics& metrics) noexcept : metrics_(&metrics) {}
    ~CounterState() noexcept override {
        if (metrics_->owner)
            metrics_->stateDestructorReentry = static_cast<int>(metrics_->owner->clearRoot());
    }
    void initState(NanoUI::BuildContext& context) noexcept override {
        ++metrics_->init;
        handle_ = context.handle();
        observe(context);
        metrics_->initReentry = static_cast<int>(context.owner().requestBuild(handle_));
        (void)context.owner().setState(handle_, shouldNotRunMutation,
                                       &metrics_->mutationCallbackCalls);
    }
    void didUpdateWidget(const NanoUI::Widget& previous, NanoUI::BuildContext&) noexcept override;
    NanoUI::BuildResult build(NanoUI::BuildContext& context) noexcept override {
        ++metrics_->builds;
        observe(context);
        metrics_->buildReentry = static_cast<int>(context.owner().requestBuild(handle_));
        (void)context.owner().setState(handle_, shouldNotRunMutation,
                                       &metrics_->mutationCallbackCalls);
        if (!metrics_->emitChild)
            return {};
        if (metrics_->emitSecondLeaf) {
            auto child =
                NanoUI::makeOwned<LeafTwo>(context.allocator(), NanoUI::AllocationKind::widget);
            if (!child)
                return {NanoUI::UiError::outOfMemory, {}};
            return {NanoUI::UiError::none, std::move(child)};
        }
        auto child =
            NanoUI::makeOwned<LeafOne>(context.allocator(), NanoUI::AllocationKind::widget);
        if (!child)
            return {NanoUI::UiError::outOfMemory, {}};
        return {NanoUI::UiError::none, std::move(child)};
    }
    void dispose() noexcept override {
        ++metrics_->dispose;
        if (metrics_->owner)
            metrics_->disposeReentry = static_cast<int>(metrics_->owner->clearRoot());
    }

   private:
    void observe(NanoUI::BuildContext& context) noexcept {
        const auto snapshot = context.owner().inspect(handle_);
        if (snapshot.error == NanoUI::UiError::none &&
            snapshot.value.phase == NanoUI::ElementPhase::created)
            ++metrics_->createdHandleObservations;
        if (metrics_->expectedOldRootSerial != 0 &&
            context.owner().root().serial == metrics_->expectedOldRootSerial)
            ++metrics_->stagedOldRootObservations;
    }
    Metrics* metrics_;
    NanoUI::ElementHandle handle_{};
};

class CounterWidget final : public NanoUI::StatefulWidget {
   public:
    CounterWidget(NanoUI::Allocator& allocator, Metrics& metrics, int version, NanoUI::Key key = {},
                  std::string_view text = {}) noexcept
        : StatefulWidget(NanoUI::typeId<CounterWidget>(), key),
          metrics_(&metrics),
          version_(version),
          text_(allocator) {
        (void)text_.assign(text);
    }
    ~CounterWidget() noexcept override {
        ++metrics_->configDestructions;
        if (metrics_->owner)
            metrics_->widgetDestructorReentry = static_cast<int>(metrics_->owner->clearRoot());
    }
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        if (metrics_->owner && metrics_->enableCreateStateProbe) {
            auto attempted = NanoUI::makeOwned<LeafOne>(allocator, NanoUI::AllocationKind::widget);
            metrics_->createStateReentry =
                static_cast<int>(metrics_->owner->setRoot(std::move(attempted)));
        }
        auto state =
            NanoUI::makeOwned<CounterState>(allocator, NanoUI::AllocationKind::state, *metrics_);
        return state;
    }
    int version() const noexcept { return version_; }
    Metrics& metrics() const noexcept { return *metrics_; }
    std::string_view text() const noexcept { return {text_.c_str(), text_.size()}; }

   private:
    Metrics* metrics_;
    int version_;
    NanoUI::OwnedText text_;
};

void CounterState::didUpdateWidget(const NanoUI::Widget& previous,
                                   NanoUI::BuildContext& context) noexcept {
    ++metrics_->update;
    auto& old = static_cast<const CounterWidget&>(previous);
    if (old.version() == 1 && old.metrics().configDestructions == 0)
        ++metrics_->oldConfigAliveInHook;
    if (!metrics_->expectedPreviousText.empty() && old.text() == metrics_->expectedPreviousText)
        ++metrics_->oldConfigAliveInHook;
    metrics_->updateReentry = static_cast<int>(context.owner().clearRoot());
    metrics_->updateRequestReentry =
        static_cast<int>(context.owner().requestBuild(context.handle()));
}

struct MutationPayload {
    Metrics* metrics;
    int amount;
    bool reenter;
};
void mutateCounter(NanoUI::State&, void* payload) noexcept {
    auto& mutation = *static_cast<MutationPayload*>(payload);
    mutation.metrics->value += mutation.amount;
    ++mutation.metrics->mutations;
    if (mutation.reenter && mutation.metrics->owner)
        mutation.metrics->mutationReentry = static_cast<int>(mutation.metrics->owner->clearRoot());
}

bool balanced(const FaultAllocator& allocator) {
    CHECK(allocator.parameterMismatches == 0);
    CHECK(allocator.unknownDeallocations == 0);
    CHECK(allocator.recordOverflow == 0);
    CHECK(allocator.allocations == allocator.deallocations);
    for (const auto& record : allocator.records)
        CHECK(!record.live);
    return failures == 0;
}

void testHandlesRetentionAndInactiveResume() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Metrics metrics;
    {
        NanoUI::BuildOwner owner(allocator);
        metrics.owner = &owner;
        auto config = NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget,
                                                       allocator, metrics, 1);
        CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::none);
        const auto handle = owner.root();
        CHECK(handle.serial != 0);
        CHECK(owner.inspect(handle).error == NanoUI::UiError::none);
        CHECK(metrics.init == 1);
        CHECK(metrics.createStateReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.createdHandleObservations >= 2);
        CHECK(metrics.stagedOldRootObservations == 0);
        CHECK(metrics.initReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.buildReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.mutationCallbackCalls == 0);

        MutationPayload mutation{&metrics, 3, true};
        CHECK(owner.setState(handle, mutateCounter, &mutation) == NanoUI::UiError::none);
        CHECK(metrics.value == 3 && metrics.mutations == 1);
        CHECK(metrics.mutationReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(owner.requestBuild(handle) == NanoUI::UiError::none);
        const auto firstPump = owner.pump();
        CHECK(firstPump.error == NanoUI::UiError::none && firstPump.builds == 1);

        CHECK(owner.setRootActive(false) == NanoUI::UiError::none);
        mutation.reenter = false;
        CHECK(owner.setState(handle, mutateCounter, &mutation) == NanoUI::UiError::none);
        CHECK(owner.pump().needsFrame == false);
        CHECK(owner.inspect(handle).error == NanoUI::UiError::none);
        CHECK(owner.setRootActive(true) == NanoUI::UiError::none);
        const auto resumed = owner.pump();
        CHECK(resumed.error == NanoUI::UiError::none && resumed.builds == 1);
        CHECK(metrics.value == 6);

        const int updatesBefore = metrics.update;
        auto compatible = NanoUI::makeOwned<CounterWidget>(
            allocator, NanoUI::AllocationKind::widget, allocator, metrics, 2);
        CHECK(owner.setRoot(std::move(compatible)) == NanoUI::UiError::none);
        CHECK(metrics.update == updatesBefore + 1);  // Synchronous setRoot hook.
        CHECK(owner.root().serial == handle.serial);
        CHECK(metrics.oldConfigAliveInHook == 1);
        CHECK(owner.pump().builds == 1);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
        CHECK(owner.inspect(handle).error == NanoUI::UiError::staleHandle);
        CHECK(metrics.dispose == 1);
        CHECK(metrics.disposeReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.stateDestructorReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.widgetDestructorReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testRootKeyTypeReplacementAndOwnedTextConfigLifetime() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Metrics metrics;
    {
        NanoUI::BuildOwner owner(allocator);
        metrics.owner = &owner;
        char source[] = "old";
        auto original = NanoUI::makeOwned<CounterWidget>(
            allocator, NanoUI::AllocationKind::widget, allocator, metrics, 1, NanoUI::Key::value(7),
            std::string_view(source, 3));
        CHECK(owner.setRoot(std::move(original)) == NanoUI::UiError::none);
        const auto originalHandle = owner.root();
        source[0] = 'X';  // Configuration owns a byte copy, not this temporary buffer.

        metrics.expectedPreviousText = "old";
        metrics.expectedOldRootSerial = originalHandle.serial;
        auto compatible =
            NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget, allocator,
                                             metrics, 2, NanoUI::Key::value(7), "new");
        CHECK(owner.setRoot(std::move(compatible)) == NanoUI::UiError::none);
        CHECK(owner.root().serial == originalHandle.serial);
        CHECK(metrics.update == 1 && metrics.oldConfigAliveInHook == 2);
        CHECK(metrics.updateReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.updateRequestReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        auto changedKey =
            NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget, allocator,
                                             metrics, 3, NanoUI::Key::value(8), "key");
        CHECK(owner.setRoot(std::move(changedKey)) == NanoUI::UiError::none);
        CHECK(metrics.stagedOldRootObservations >= 2);
        metrics.expectedOldRootSerial = 0;
        CHECK(owner.root().serial != originalHandle.serial);
        CHECK(owner.inspect(originalHandle).error == NanoUI::UiError::staleHandle);
        CHECK(metrics.init == 2 && metrics.dispose == 1);

        const auto statefulHandle = owner.root();
        auto changedType = NanoUI::makeOwned<LeafTwo>(allocator, NanoUI::AllocationKind::widget);
        CHECK(owner.setRoot(std::move(changedType)) == NanoUI::UiError::none);
        CHECK(owner.inspect(statefulHandle).error == NanoUI::UiError::staleHandle);
        const auto snapshot = owner.inspect(owner.root());
        CHECK(snapshot.error == NanoUI::UiError::none);
        CHECK(snapshot.value.type == NanoUI::typeId<LeafTwo>());
        CHECK(!snapshot.value.hasState);
        CHECK(metrics.dispose == 2);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testOwnerDestructorGuardsDerivedDestructors() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Metrics metrics;
    {
        NanoUI::BuildOwner owner(allocator);
        metrics.owner = &owner;
        auto config = NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget,
                                                       allocator, metrics, 1);
        CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::none);
    }
    CHECK(metrics.disposeReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
    CHECK(metrics.stateDestructorReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
    CHECK(metrics.widgetDestructorReentry == static_cast<int>(NanoUI::UiError::invalidPhase));
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

struct FailedStageMetrics {
    NanoUI::BuildOwner* owner = nullptr;
    NanoUI::ElementHandle parent{};
    NanoUI::ElementHandle child{};
    int sequence = 0;
    int childDisposedAt = 0;
    int parentDisposedAt = 0;
    int reentry = 0;
};

class FailingChildState final : public NanoUI::State {
   public:
    explicit FailingChildState(FailedStageMetrics& metrics) noexcept : metrics_(&metrics) {}
    void initState(NanoUI::BuildContext& context) noexcept override {
        metrics_->child = context.handle();
    }
    NanoUI::BuildResult build(NanoUI::BuildContext&) noexcept override {
        return {NanoUI::UiError::buildFailed, {}};
    }
    void dispose() noexcept override {
        CHECK(metrics_->owner->inspect(metrics_->parent).error == NanoUI::UiError::staleHandle);
        CHECK(metrics_->owner->inspect(metrics_->child).error == NanoUI::UiError::staleHandle);
        metrics_->childDisposedAt = ++metrics_->sequence;
    }

   private:
    FailedStageMetrics* metrics_;
};

class FailingChildWidget final : public NanoUI::StatefulWidget {
   public:
    explicit FailingChildWidget(FailedStageMetrics& metrics) noexcept
        : StatefulWidget(NanoUI::typeId<FailingChildWidget>()), metrics_(&metrics) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        return NanoUI::makeOwned<FailingChildState>(allocator, NanoUI::AllocationKind::state,
                                                    *metrics_);
    }

   private:
    FailedStageMetrics* metrics_;
};

class FailingParentState final : public NanoUI::State {
   public:
    explicit FailingParentState(FailedStageMetrics& metrics) noexcept : metrics_(&metrics) {}
    void initState(NanoUI::BuildContext& context) noexcept override {
        metrics_->parent = context.handle();
    }
    NanoUI::BuildResult build(NanoUI::BuildContext& context) noexcept override {
        auto child = NanoUI::makeOwned<FailingChildWidget>(
            context.allocator(), NanoUI::AllocationKind::widget, *metrics_);
        if (!child)
            return {NanoUI::UiError::outOfMemory, {}};
        return {NanoUI::UiError::none, std::move(child)};
    }
    void dispose() noexcept override { metrics_->parentDisposedAt = ++metrics_->sequence; }

   private:
    FailedStageMetrics* metrics_;
};

class FailingParentWidget final : public NanoUI::StatefulWidget {
   public:
    explicit FailingParentWidget(FailedStageMetrics& metrics) noexcept
        : StatefulWidget(NanoUI::typeId<FailingParentWidget>()), metrics_(&metrics) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        if (metrics_->owner) {
            auto nested = NanoUI::makeOwned<LeafOne>(allocator, NanoUI::AllocationKind::widget);
            metrics_->reentry = static_cast<int>(metrics_->owner->setRoot(std::move(nested)));
        }
        return NanoUI::makeOwned<FailingParentState>(allocator, NanoUI::AllocationKind::state,
                                                     *metrics_);
    }

   private:
    FailedStageMetrics* metrics_;
};

void testFailedStageInvalidatesAllHandlesBeforeChildFirstDispose() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    FailedStageMetrics metrics;
    {
        NanoUI::BuildOwner owner(allocator);
        metrics.owner = &owner;
        auto config = NanoUI::makeOwned<FailingParentWidget>(
            allocator, NanoUI::AllocationKind::widget, metrics);
        CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::buildFailed);
        CHECK(!owner.root().owner);
        CHECK(metrics.reentry == static_cast<int>(NanoUI::UiError::invalidPhase));
        CHECK(metrics.childDisposedAt == 1);
        CHECK(metrics.parentDisposedAt == 2);
        CHECK(owner.inspect(metrics.parent).error == NanoUI::UiError::staleHandle);
        CHECK(owner.inspect(metrics.child).error == NanoUI::UiError::staleHandle);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testForeignRemovedAndLeafHandles() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    {
        NanoUI::BuildOwner first(allocator);
        NanoUI::BuildOwner second(allocator);
        auto leaf = NanoUI::makeOwned<LeafOne>(allocator, NanoUI::AllocationKind::widget);
        CHECK(first.setRoot(std::move(leaf)) == NanoUI::UiError::none);
        const auto handle = first.root();
        CHECK(second.inspect(handle).error == NanoUI::UiError::staleHandle);
        CHECK(first.setState(handle, mutateCounter, nullptr) == NanoUI::UiError::invalidInput);
        CHECK(first.clearRoot() == NanoUI::UiError::none);
        CHECK(first.inspect(handle).error == NanoUI::UiError::staleHandle);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testFailedRootElementAllocationKeepsOldRootDuringConfigDestruction() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    DestructorReentry reentry;
    {
        NanoUI::BuildOwner owner(allocator);
        reentry.owner = &owner;
        auto oldRoot = NanoUI::makeOwned<LeafOne>(allocator, NanoUI::AllocationKind::widget,
                                                  NanoUI::Key::value(1));
        CHECK(owner.setRoot(std::move(oldRoot)) == NanoUI::UiError::none);
        const NanoUI::ElementHandle oldHandle = owner.root();

        auto replacement =
            NanoUI::makeOwned<ReentrantLeaf>(allocator, NanoUI::AllocationKind::widget, reentry);
        upstream.failAt = upstream.calls + 1;  // Fail precisely at replacement Element allocation.
        const NanoUI::UiError result = owner.setRoot(std::move(replacement));
        upstream.failAt = 0;

        CHECK(result == NanoUI::UiError::outOfMemory);
        CHECK(reentry.calls == 1);
        CHECK(reentry.result == NanoUI::UiError::invalidPhase);
        CHECK(owner.root().serial == oldHandle.serial);
        CHECK(owner.inspect(oldHandle).error == NanoUI::UiError::none);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

struct TreeMetrics {
    bool emitLeaf = false;
};

class BranchState final : public NanoUI::State {
   public:
    explicit BranchState(TreeMetrics& metrics) noexcept : metrics_(&metrics) {}
    NanoUI::BuildResult build(NanoUI::BuildContext& context) noexcept override {
        if (!metrics_->emitLeaf)
            return {};
        auto leaf = NanoUI::makeOwned<LeafOne>(context.allocator(), NanoUI::AllocationKind::widget);
        if (!leaf)
            return {NanoUI::UiError::outOfMemory, {}};
        return {NanoUI::UiError::none, std::move(leaf)};
    }

   private:
    TreeMetrics* metrics_;
};

class BranchWidget final : public NanoUI::StatefulWidget {
   public:
    explicit BranchWidget(TreeMetrics& metrics) noexcept
        : StatefulWidget(NanoUI::typeId<BranchWidget>()), metrics_(&metrics) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        return NanoUI::makeOwned<BranchState>(allocator, NanoUI::AllocationKind::state, *metrics_);
    }

   private:
    TreeMetrics* metrics_;
};

class TreeRootState final : public NanoUI::State {
   public:
    explicit TreeRootState(TreeMetrics& metrics) noexcept : metrics_(&metrics) {}
    NanoUI::BuildResult build(NanoUI::BuildContext& context) noexcept override {
        auto child = NanoUI::makeOwned<BranchWidget>(context.allocator(),
                                                     NanoUI::AllocationKind::widget, *metrics_);
        if (!child)
            return {NanoUI::UiError::outOfMemory, {}};
        return {NanoUI::UiError::none, std::move(child)};
    }

   private:
    TreeMetrics* metrics_;
};

class TreeRootWidget final : public NanoUI::StatefulWidget {
   public:
    explicit TreeRootWidget(TreeMetrics& metrics) noexcept
        : StatefulWidget(NanoUI::typeId<TreeRootWidget>()), metrics_(&metrics) {}
    NanoUI::Owned<NanoUI::State> createState(NanoUI::Allocator& allocator) const noexcept override {
        return NanoUI::makeOwned<TreeRootState>(allocator, NanoUI::AllocationKind::state,
                                                *metrics_);
    }

   private:
    TreeMetrics* metrics_;
};

void testBudgetDepthAndPendingDirtyWork() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Metrics metrics;
    {
        NanoUI::BuildOwner limited(allocator, NanoUI::OwnerOptions{1, 1});
        metrics.owner = &limited;
        metrics.emitChild = true;
        auto config = NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget,
                                                       allocator, metrics, 1);
        CHECK(limited.setRoot(std::move(config)) == NanoUI::UiError::capacityExceeded);
        CHECK(!limited.root().owner);
        CHECK(metrics.dispose == 1);
        metrics.emitChild = false;
    }
    metrics = Metrics{};
    {
        NanoUI::BuildOwner depthLimited(allocator, NanoUI::OwnerOptions{1, 8});
        metrics.owner = &depthLimited;
        metrics.emitChild = true;
        auto config = NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget,
                                                       allocator, metrics, 1);
        CHECK(depthLimited.setRoot(std::move(config)) == NanoUI::UiError::capacityExceeded);
        CHECK(!depthLimited.root().owner);
    }
    TreeMetrics tree;
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions{3, 2});
        auto config =
            NanoUI::makeOwned<TreeRootWidget>(allocator, NanoUI::AllocationKind::widget, tree);
        CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::none);  // Two-level state tree.
        const auto root = owner.root();
        const auto branch = owner.inspect(root).value.child;
        tree.emitLeaf = true;
        CHECK(owner.requestBuild(branch) == NanoUI::UiError::none);
        const auto addLeaf = owner.pump();
        CHECK(addLeaf.error == NanoUI::UiError::none && addLeaf.builds == 2);
        const auto leaf = owner.inspect(branch).value.child;
        CHECK(leaf.serial != 0);

        CHECK(owner.requestBuild(root) == NanoUI::UiError::none);
        CHECK(owner.requestBuild(branch) == NanoUI::UiError::none);
        CHECK(owner.requestBuild(leaf) == NanoUI::UiError::none);
        const auto bounded = owner.pump();
        CHECK(bounded.error == NanoUI::UiError::none && bounded.builds == 2);
        CHECK(bounded.needsFrame);
        CHECK(owner.inspect(leaf).value.dirty);
        const auto resumed = owner.pump();
        CHECK(resumed.error == NanoUI::UiError::none && resumed.builds == 1);
        CHECK(!resumed.needsFrame && !owner.inspect(leaf).value.dirty);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

void testAllocationFailuresAndRetry() {
    for (std::size_t offset = 1; offset <= 4; ++offset) {
        FaultAllocator upstream;
        NanoUI::TrackingAllocator allocator(upstream);
        Metrics metrics;
        {
            NanoUI::BuildOwner owner(allocator);
            metrics.owner = &owner;
            metrics.emitChild = true;
            metrics.enableCreateStateProbe = false;
            auto config = NanoUI::makeOwned<CounterWidget>(
                allocator, NanoUI::AllocationKind::widget, allocator, metrics, 1);
            upstream.failAt = upstream.calls + offset;
            CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::outOfMemory);
            upstream.failAt = 0;
            CHECK(!owner.root().owner);
            CHECK(metrics.dispose == (offset >= 3 ? 1 : 0));
        }
        CHECK(allocator.statistics().currentBlocks() == 0);
        balanced(upstream);
    }

    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    {
        upstream.failAt = 1;
        NanoUI::BuildOwner unavailable(allocator);
        auto widget = NanoUI::makeOwned<LeafOne>(allocator, NanoUI::AllocationKind::widget);
        CHECK(unavailable.setRoot(std::move(widget)) == NanoUI::UiError::outOfMemory);
        CHECK(!unavailable.root().owner);
        CHECK(unavailable.pump().error == NanoUI::UiError::outOfMemory);
        upstream.failAt = 0;
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);

    FaultAllocator retryUpstream;
    NanoUI::TrackingAllocator retryAllocator(retryUpstream);
    Metrics metrics;
    {
        NanoUI::BuildOwner owner(retryAllocator);
        metrics.owner = &owner;
        metrics.emitChild = true;
        auto config = NanoUI::makeOwned<CounterWidget>(
            retryAllocator, NanoUI::AllocationKind::widget, retryAllocator, metrics, 1);
        CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::none);
        const auto parent = owner.root();
        const auto oldChild = owner.inspect(parent).value.child;
        auto compatible = NanoUI::makeOwned<CounterWidget>(
            retryAllocator, NanoUI::AllocationKind::widget, retryAllocator, metrics, 2);
        CHECK(owner.setRoot(std::move(compatible)) == NanoUI::UiError::none);
        CHECK(metrics.update == 1);  // Committed compatible config update invokes the hook now.
        metrics.emitSecondLeaf = true;
        MutationPayload payload{&metrics, 1, false};
        CHECK(owner.setState(parent, mutateCounter, &payload) == NanoUI::UiError::none);
        retryUpstream.failAt =
            retryUpstream.calls + 2;  // New Widget succeeds; staged Element fails.
        const auto failedPump = owner.pump();
        CHECK(failedPump.error == NanoUI::UiError::outOfMemory);
        CHECK(owner.inspect(oldChild).error == NanoUI::UiError::none);
        CHECK(owner.inspect(parent).value.dirty);
        retryUpstream.failAt = 0;
        const auto retry = owner.pump();
        CHECK(retry.error == NanoUI::UiError::none);
        CHECK(metrics.update == 1);  // Retry does not replay didUpdateWidget.
        CHECK(owner.inspect(oldChild).error == NanoUI::UiError::staleHandle);
        CHECK(owner.inspect(parent).value.child.serial != oldChild.serial);
        CHECK(metrics.value == 1);
    }
    CHECK(retryAllocator.statistics().currentBlocks() == 0);
    balanced(retryUpstream);
}

void testZeroOptionsAndMutationCoalescing() {
    FaultAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    Metrics metrics;
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions{0, 0});
        auto config = NanoUI::makeOwned<CounterWidget>(allocator, NanoUI::AllocationKind::widget,
                                                       allocator, metrics, 1);
        CHECK(owner.setRoot(std::move(config)) == NanoUI::UiError::invalidInput);
        CHECK(owner.pump().error == NanoUI::UiError::invalidInput);
        CHECK(owner.clearRoot() == NanoUI::UiError::invalidInput);
        CHECK(!owner.root().owner);
        CHECK(owner.inspect({}).error == NanoUI::UiError::staleHandle);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    balanced(upstream);
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<NanoUI::BuildOwner>);
    static_assert(!std::is_move_constructible_v<NanoUI::BuildOwner>);
    static_assert(!std::is_copy_constructible_v<NanoUI::Widget>);
    static_assert(!std::is_copy_constructible_v<NanoUI::State>);
    testHandlesRetentionAndInactiveResume();
    testRootKeyTypeReplacementAndOwnedTextConfigLifetime();
    testOwnerDestructorGuardsDerivedDestructors();
    testFailedStageInvalidatesAllHandlesBeforeChildFirstDispose();
    testForeignRemovedAndLeafHandles();
    testFailedRootElementAllocationKeepsOldRootDuringConfigDestruction();
    testBudgetDepthAndPendingDirtyWork();
    testAllocationFailuresAndRetry();
    testZeroOptionsAndMutationCoalescing();
    if (failures != 0)
        std::cerr << failures << " lifecycle check(s) failed\n";
    else
        std::cout << "NanoUI lifecycle: 9 cases / " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
