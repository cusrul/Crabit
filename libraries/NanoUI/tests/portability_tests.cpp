#include <NanoUI/foundation/identity.h>
#include <NanoUI/foundation/owned_text.h>
#include <NanoUI/foundation/text_view.h>

#include <cstddef>
#include <cstdlib>
#include <iostream>

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

class TestAllocator final : public NanoUI::Allocator {
   public:
    void* allocate(std::size_t bytes, std::size_t alignment,
                   NanoUI::AllocationKind kind) noexcept override {
        ++allocations;
        lastBytes = bytes;
        lastAlignment = alignment;
        lastKind = kind;
        return fail ? nullptr : std::malloc(bytes);
    }

    void deallocate(void* pointer, std::size_t bytes, std::size_t alignment,
                    NanoUI::AllocationKind kind) noexcept override {
        ++deallocations;
        freedBytes = bytes;
        freedAlignment = alignment;
        freedKind = kind;
        std::free(pointer);
    }

    bool fail = false;
    std::size_t allocations = 0;
    std::size_t deallocations = 0;
    std::size_t lastBytes = 0;
    std::size_t lastAlignment = 0;
    NanoUI::AllocationKind lastKind = NanoUI::AllocationKind::count;
    std::size_t freedBytes = 0;
    std::size_t freedAlignment = 0;
    NanoUI::AllocationKind freedKind = NanoUI::AllocationKind::count;
};

struct ModernView {
    const char* data() const noexcept { return bytes; }
    std::size_t size() const noexcept { return length; }
    const char* bytes;
    std::size_t length;
};

struct OtherType {};

void testTextViewBoundaries() {
    const NanoUI::TextView empty;
    CHECK(empty.data() == nullptr);
    CHECK(empty.size() == 0);
    CHECK(empty.empty());

    const NanoUI::TextView cString("hello");
    CHECK(cString.size() == 5);
    CHECK(cString.data()[cString.size()] == '\0');

    const char bytes[] = {'a', '\0', 'b', 'x'};
    const NanoUI::TextView bounded(bytes, 3);
    CHECK(bounded.data() == bytes);
    CHECK(bounded.size() == 3);
    CHECK(!bounded.empty());

    const ModernView modern{bytes, 2};
    const NanoUI::TextView adapted(modern);
    CHECK(adapted.data() == bytes);
    CHECK(adapted.size() == 2);
}

void testOwnedTextInputAndLifetime() {
    TestAllocator allocator;
    {
        NanoUI::OwnedText text(allocator);
        char source[] = "owned bytes";
        CHECK(text.assign(source) == NanoUI::TextResult::ok);
        source[0] = 'X';
        CHECK(text.size() == 11);
        CHECK(text.c_str()[0] == 'o');
        CHECK(text.c_str()[text.size()] == '\0');
        CHECK(allocator.lastBytes == 12);
        CHECK(allocator.lastAlignment == alignof(char));
        CHECK(allocator.lastKind == NanoUI::AllocationKind::resource);

        const NanoUI::TextView alias(text.c_str() + 6, 5);
        CHECK(text.assign(alias) == NanoUI::TextResult::ok);
        CHECK(text.size() == 5);
        CHECK(text.c_str()[0] == 'b' && text.c_str()[4] == 's');

        const char embeddedNul[] = {'n', '\0', 'o'};
        CHECK(text.assign(NanoUI::TextView(embeddedNul, sizeof(embeddedNul))) ==
              NanoUI::TextResult::invalidInput);
        CHECK(text.assign(NanoUI::TextView(nullptr, 1)) == NanoUI::TextResult::invalidInput);
        CHECK(text.size() == 5 && text.c_str()[0] == 'b');

        allocator.fail = true;
        CHECK(text.assign(NanoUI::TextView("replacement")) == NanoUI::TextResult::outOfMemory);
        CHECK(text.size() == 5 && text.c_str()[0] == 'b');
        allocator.fail = false;

        CHECK(text.assign(NanoUI::TextView()) == NanoUI::TextResult::ok);
        CHECK(text.size() == 0 && text.c_str()[0] == '\0');
    }
    CHECK(allocator.allocations == allocator.deallocations + 1);
    CHECK(allocator.freedBytes == 6);
    CHECK(allocator.freedAlignment == alignof(char));
    CHECK(allocator.freedKind == NanoUI::AllocationKind::resource);
}

void testTypeIdentityCanonicalization() {
    CHECK(NanoUI::typeId<const OtherType&>() == NanoUI::typeId<OtherType>());
    CHECK(NanoUI::typeId<volatile OtherType>() == NanoUI::typeId<OtherType>());
    CHECK(NanoUI::typeId<OtherType>() != NanoUI::typeId<int>());
}

void testAllocationCategoryStorage() {
    NanoUI::MemoryStatistics statistics;
    const std::size_t count = static_cast<std::size_t>(NanoUI::AllocationKind::count);
    CHECK(statistics.categories.size() == count);
    CHECK(statistics.categories.end() - statistics.categories.begin() ==
          static_cast<std::ptrdiff_t>(count));
    for (const auto& category : statistics.categories) {
        CHECK(category.bytes == 0);
        CHECK(category.blocks == 0);
    }
    statistics.categories[static_cast<std::size_t>(NanoUI::AllocationKind::renderObject)].bytes =
        19;
    statistics.categories[static_cast<std::size_t>(NanoUI::AllocationKind::renderObject)].blocks =
        2;
    const auto& constant = statistics.categories;
    CHECK(constant.end() - constant.begin() == static_cast<std::ptrdiff_t>(count));
    CHECK(constant[static_cast<std::size_t>(NanoUI::AllocationKind::renderObject)].bytes == 19);
    CHECK(statistics.currentBytes() == 19);
    CHECK(statistics.currentBlocks() == 2);
    auto copy = statistics;
    copy.categories[static_cast<std::size_t>(NanoUI::AllocationKind::renderObject)].bytes = 7;
    CHECK(copy.currentBytes() == 7);
    CHECK(statistics.currentBytes() == 19);
}

}  // namespace

int main() {
    testTextViewBoundaries();
    testOwnedTextInputAndLifetime();
    testTypeIdentityCanonicalization();
    testAllocationCategoryStorage();
    std::cout << "NanoUI portability: " << (checks - failures) << '/' << checks
              << " checks passed\n";
    return failures == 0 ? 0 : 1;
}
