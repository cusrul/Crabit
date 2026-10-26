#include <NanoUI/foundation/allocator.h>
#include <NanoUI/foundation/identity.h>
#include <NanoUI/foundation/owned.h>
#include <NanoUI/foundation/owned_text.h>
#include <windows/heap_allocator.h>
#include "identity_fixture.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <type_traits>

namespace {

int failures = 0;
int checks = 0;

#define CHECK(expression)                                                                    \
    do {                                                                                     \
        ++checks;                                                                            \
        if (!(expression)) {                                                                 \
            std::cerr << __FILE__ << ':' << __LINE__ << ": CHECK failed: " #expression "\n"; \
            ++failures;                                                                      \
        }                                                                                    \
    } while (false)

class FailingAllocator final : public NanoUI::Allocator {
   public:
    void* allocate(std::size_t bytes, std::size_t alignment,
                   NanoUI::AllocationKind kind) noexcept override {
        if (fail)
            return nullptr;
        return heap.allocate(bytes, alignment, kind);
    }
    void deallocate(void* pointer, std::size_t bytes, std::size_t alignment,
                    NanoUI::AllocationKind kind) noexcept override {
        heap.deallocate(pointer, bytes, alignment, kind);
    }
    bool fail = false;

   private:
    NanoUI::Windows::HeapAllocator heap;
};

bool testCategoryAccountingAndOom() {
    FailingAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    constexpr auto categoryCount = static_cast<std::size_t>(NanoUI::AllocationKind::count);
    void* allocations[categoryCount]{};
    const auto& initial = allocator.statistics();
    CHECK(initial.currentBytes() == 0);
    CHECK(initial.currentBlocks() == 0);
    for (std::size_t i = 0; i < categoryCount; ++i) {
        const auto kind = static_cast<NanoUI::AllocationKind>(i);
        const auto bytes = 11 + i;
        allocations[i] = allocator.allocate(bytes, alignof(std::max_align_t), kind);
        CHECK(allocations[i] != nullptr);
        CHECK(allocator.statistics().categories[i].bytes == bytes);
        CHECK(allocator.statistics().categories[i].blocks == 1);
    }
    const auto& full = allocator.statistics();
    CHECK(full.currentBlocks() == categoryCount);
    std::size_t expectedBytes = 0;
    for (std::size_t i = 0; i < categoryCount; ++i)
        expectedBytes += 11 + i;
    CHECK(full.currentBytes() == expectedBytes);
    CHECK(full.peakBytes == expectedBytes);
    CHECK(full.peakBlocks == categoryCount);

    upstream.fail = true;
    CHECK(allocator.allocate(8, 8, NanoUI::AllocationKind::resource) == nullptr);
    CHECK(allocator.statistics().failedAllocations == 1);
    upstream.fail = false;
    for (std::size_t i = categoryCount; i > 0; --i) {
        const auto index = i - 1;
        allocator.deallocate(allocations[index], 11 + index, alignof(std::max_align_t),
                             static_cast<NanoUI::AllocationKind>(index));
    }
    CHECK(allocator.statistics().currentBytes() == 0);
    CHECK(allocator.statistics().currentBlocks() == 0);
    for (const auto& category : allocator.statistics().categories) {
        CHECK(category.bytes == 0);
        CHECK(category.blocks == 0);
    }
    CHECK(allocator.statistics().peakBytes == expectedBytes);
    CHECK(allocator.statistics().peakBlocks == categoryCount);
    return failures == 0;
}

struct alignas(32) Aligned32 {
    std::uint8_t payload[32]{};
};
struct alignas(64) Aligned64 {
    std::uint8_t payload[64]{};
};

bool testOwnedAlignmentMoveAndReset() {
    NanoUI::Windows::HeapAllocator allocator;
    auto aligned32 = NanoUI::makeOwned<Aligned32>(allocator, NanoUI::AllocationKind::resource);
    auto aligned64 = NanoUI::makeOwned<Aligned64>(allocator, NanoUI::AllocationKind::resource);
    CHECK(aligned32);
    CHECK(aligned64);
    CHECK(reinterpret_cast<std::uintptr_t>(aligned32.get()) % 32 == 0);
    CHECK(reinterpret_cast<std::uintptr_t>(aligned64.get()) % 64 == 0);

    auto moved = std::move(aligned32);
    CHECK(!aligned32);
    CHECK(moved);
    CHECK(reinterpret_cast<std::uintptr_t>(moved.get()) % 32 == 0);
    moved.reset();
    moved.reset();
    CHECK(!moved);
    CHECK(aligned64);
    return failures == 0;
}

struct Prefix {
    virtual ~Prefix() noexcept = default;
    std::uintptr_t marker = 0;
};
struct Base {
    virtual ~Base() noexcept = default;
};
struct Derived final : Prefix, Base {
    explicit Derived(int& destroyed) noexcept : destroyed_(&destroyed) {}
    ~Derived() noexcept override { ++*destroyed_; }
    int* destroyed_;
};

bool testDerivedToBaseRetainsCompleteObject() {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    int destroyed = 0;
    void* completeAddress = nullptr;
    {
        auto derived =
            NanoUI::makeOwned<Derived>(allocator, NanoUI::AllocationKind::widget, destroyed);
        completeAddress = derived.get();
        NanoUI::Owned<Base> base(std::move(derived));
        CHECK(!derived);
        CHECK(base);
        CHECK(static_cast<void*>(base.get()) != completeAddress);
        CHECK(allocator.statistics().currentBlocks() == 1);
        base.reset();
        CHECK(destroyed == 1);
        CHECK(allocator.statistics().currentBlocks() == 0);
    }
    CHECK(destroyed == 1);
    return failures == 0;
}

bool testOwnedTextCopiesBytesAndPreservesOnFailure() {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    NanoUI::OwnedText text(allocator);
    CHECK(text.size() == 0);
    CHECK(std::strcmp(text.c_str(), "") == 0);
    CHECK(allocator.statistics().currentBlocks() == 0);
    CHECK(text.assign(std::string("temporary source")) == NanoUI::TextResult::ok);
    CHECK(std::string_view(text.c_str(), text.size()) == "temporary source");

    const char embeddedNul[] = {'x', '\0', 'y'};
    CHECK(text.assign(std::string_view(embeddedNul, sizeof(embeddedNul))) ==
          NanoUI::TextResult::invalidInput);
    CHECK(std::string_view(text.c_str(), text.size()) == "temporary source");

    const char arbitraryBytes[] = {static_cast<char>(0xff), static_cast<char>(0x80)};
    CHECK(text.assign(std::string_view(arbitraryBytes, sizeof(arbitraryBytes))) ==
          NanoUI::TextResult::ok);
    CHECK(text.size() == sizeof(arbitraryBytes));
    CHECK(std::memcmp(text.c_str(), arbitraryBytes, sizeof(arbitraryBytes)) == 0);

    CHECK(text.assign("original") == NanoUI::TextResult::ok);
    FailingAllocator failingUpstream;
    NanoUI::TrackingAllocator failing(failingUpstream);
    NanoUI::OwnedText oomText(failing);
    CHECK(oomText.assign("stable") == NanoUI::TextResult::ok);
    failingUpstream.fail = true;
    CHECK(oomText.assign(std::string_view(oomText.c_str() + 1, 3)) ==
          NanoUI::TextResult::outOfMemory);
    CHECK(std::string_view(oomText.c_str(), oomText.size()) == "stable");
    failingUpstream.fail = false;
    CHECK(oomText.assign(std::string_view(oomText.c_str() + 1, 3)) == NanoUI::TextResult::ok);
    CHECK(std::string_view(oomText.c_str(), oomText.size()) == "tab");
    CHECK(oomText.assign(std::string_view{}) == NanoUI::TextResult::ok);
    CHECK(oomText.size() == 0);
    CHECK(failing.statistics().currentBlocks() == 0);
    return failures == 0;
}

bool testOwnedTextMoveBetweenAllocators() {
    NanoUI::Windows::HeapAllocator heapA;
    NanoUI::Windows::HeapAllocator heapB;
    NanoUI::TrackingAllocator allocatorA(heapA);
    NanoUI::TrackingAllocator allocatorB(heapB);
    {
        NanoUI::OwnedText destination(allocatorA);
        NanoUI::OwnedText source(allocatorB);
        CHECK(destination.assign("old-A") == NanoUI::TextResult::ok);
        CHECK(source.assign("from-B") == NanoUI::TextResult::ok);
        destination = std::move(source);
        CHECK(std::string_view(destination.c_str(), destination.size()) == "from-B");
        CHECK(source.size() == 0);
        CHECK(allocatorA.statistics().currentBlocks() == 0);
        CHECK(allocatorB.statistics().currentBlocks() == 1);
        CHECK(source.assign("reused-B") == NanoUI::TextResult::ok);
        CHECK(std::string_view(source.c_str(), source.size()) == "reused-B");
    }
    CHECK(allocatorA.statistics().currentBlocks() == 0);
    CHECK(allocatorB.statistics().currentBlocks() == 0);
    return failures == 0;
}

bool testKeyAndTypeIdentity() {
    CHECK(NanoUI::Key{} != NanoUI::Key::value(0));
    CHECK(NanoUI::Key::value(0) == NanoUI::Key::value(0));
    CHECK(NanoUI::Key::value(0) != NanoUI::Key::value(1));
    CHECK(NanoUI::typeId<IdentitySample>() == identityPeer());
    CHECK(NanoUI::typeId<const IdentitySample&>() == NanoUI::typeId<IdentitySample>());
    CHECK(NanoUI::typeId<IdentitySample>() != NanoUI::typeId<int>());
    return failures == 0;
}

struct Counted {
    Counted(int& constructed, int& destroyed) noexcept : destroyed_(&destroyed) { ++constructed; }
    ~Counted() noexcept { ++*destroyed_; }
    int* destroyed_;
};

bool testFactoryOomAndAllocatorValidation() {
    static_assert(!std::is_copy_constructible_v<NanoUI::TrackingAllocator>);
    static_assert(!std::is_move_constructible_v<NanoUI::TrackingAllocator>);
    static_assert(!std::is_copy_constructible_v<NanoUI::Owned<Counted>>);
    FailingAllocator upstream;
    NanoUI::TrackingAllocator allocator(upstream);
    int constructed = 0;
    int destroyed = 0;
    upstream.fail = true;
    auto missing = NanoUI::makeOwned<Counted>(allocator, NanoUI::AllocationKind::widget,
                                              constructed, destroyed);
    CHECK(!missing);
    CHECK(constructed == 0 && destroyed == 0);
    CHECK(allocator.statistics().failedAllocations == 1);
    upstream.fail = false;
    CHECK(allocator.allocate(8, 3, NanoUI::AllocationKind::widget) == nullptr);
    CHECK(allocator.allocate(0, 8, NanoUI::AllocationKind::widget) == nullptr);
    CHECK(allocator.allocate(8, 8, NanoUI::AllocationKind::count) == nullptr);
    CHECK(allocator.statistics().failedAllocations == 4);
    {
        auto first = NanoUI::makeOwned<Counted>(allocator, NanoUI::AllocationKind::state,
                                                constructed, destroyed);
        auto second = NanoUI::makeOwned<Counted>(allocator, NanoUI::AllocationKind::state,
                                                 constructed, destroyed);
        CHECK(first && second && constructed == 2);
        first = std::move(second);
        CHECK(destroyed == 1 && !second && first);
        CHECK(allocator.statistics().currentBlocks() == 1);
        first = std::move(first);
        CHECK(first && destroyed == 1);
    }
    CHECK(destroyed == 2);
    CHECK(allocator.statistics().currentBytes() == 0);
    return failures == 0;
}

struct NonVirtualPrefix {
    int padding[8]{};
};
struct NonVirtualBase {
    explicit NonVirtualBase(int& destroyed) noexcept : destroyed_(&destroyed) {}
    ~NonVirtualBase() noexcept { ++*destroyed_; }
    int* destroyed_;
};
struct NonVirtualDerived : NonVirtualPrefix, NonVirtualBase {
    NonVirtualDerived(int& baseDestroyed, int& derivedDestroyed) noexcept
        : NonVirtualBase(baseDestroyed), destroyed_(&derivedDestroyed) {}
    ~NonVirtualDerived() noexcept { ++*destroyed_; }
    int* destroyed_;
};

bool testNonVirtualBaseAndConvertingAssignment() {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    int baseDestroyed = 0;
    int derivedDestroyed = 0;
    {
        auto derived = NanoUI::makeOwned<NonVirtualDerived>(
            allocator, NanoUI::AllocationKind::element, baseDestroyed, derivedDestroyed);
        void* complete = derived.get();
        NanoUI::Owned<NonVirtualBase> base;
        base = std::move(derived);
        CHECK(base && !derived);
        CHECK(static_cast<void*>(base.get()) != complete);
    }
    CHECK(baseDestroyed == 1 && derivedDestroyed == 1);
    CHECK(allocator.statistics().currentBlocks() == 0);
    return failures == 0;
}

}  // namespace

int main() {
    testCategoryAccountingAndOom();
    testOwnedAlignmentMoveAndReset();
    testDerivedToBaseRetainsCompleteObject();
    testOwnedTextCopiesBytesAndPreservesOnFailure();
    testOwnedTextMoveBetweenAllocators();
    testKeyAndTypeIdentity();
    testFactoryOomAndAllocatorValidation();
    testNonVirtualBaseAndConvertingAssignment();
    if (failures != 0)
        std::cerr << failures << " foundation check(s) failed\n";
    else
        std::cout << "NanoUI foundation: 8 cases / " << checks << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
