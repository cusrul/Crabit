#include <hal/canvas.h>
#include <NanoUI/ports/legacy_canvas.h>
#include <NanoUI/ports/legacy_bitmap_font.h>
#include <NanoUI/navigation/navigator.h>
#include <NanoUI/framework/build_owner.h>
#include <pages/settings_hub.h>
#include <pages/settings_hub_route.h>
#include <windows/heap_allocator.h>
#include <windows/raster_canvas.h>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace {
int checks = 0, failures = 0;
#define CHECK(v)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(v)) {                                \
            ++failures;                            \
            std::printf("%d: %s\n", __LINE__, #v); \
        }                                          \
    } while (false)

class MemoryCanvas final : public Canvas {
   public:
    MemoryCanvas() { resetClipRect(); }
    void clear() override {
        ++clears;
        std::memset(pixels, 0, sizeof pixels);
    }
    void display() override { ++displays; }
    int16_t width() override { return 128; }
    int16_t height() override { return 96; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && x < 128 && y >= 0 && y < 96)
            pixels[y * 128 + x] = color == C_WHITE;
    }
    std::uint8_t pixels[128 * 96]{};
    int clears = 0, displays = 0;
};

class DummyPage final : public NanoUI::StatelessWidget {
   public:
    DummyPage() noexcept : StatelessWidget(NanoUI::typeId<DummyPage>()) {}
    NanoUI::BuildResult build(NanoUI::BuildContext& context) const noexcept override {
        auto widget = NanoUI::makeOwned<NanoUI::SizedBox>(
            context.allocator(), NanoUI::AllocationKind::widget, NanoUI::Size(128, 96),
            NanoUI::ChildBuilder(fill, nullptr));
        const auto error = widget ? NanoUI::UiError::none : NanoUI::UiError::outOfMemory;
        return NanoUI::BuildResult(error, std::move(widget));
    }

   private:
    static NanoUI::BuildResult fill(NanoUI::BuildContext& context, void*) noexcept {
        auto widget = NanoUI::makeOwned<NanoUI::DecoratedBox>(
            context.allocator(), NanoUI::AllocationKind::widget, NanoUI::BoxStyle(0xFFFFFFFFu),
            NanoUI::ChildBuilder{});
        const auto error = widget ? NanoUI::UiError::none : NanoUI::UiError::outOfMemory;
        return NanoUI::BuildResult(error, std::move(widget));
    }
};

struct Actions {
    int calls[5]{};
    CrabitUI::SettingsDestination last = CrabitUI::SettingsDestination::exit;
    bool accept = true;
    NanoUI::Navigator* navigator = nullptr;
    NanoUI::Allocator* allocator = nullptr;
    static bool invoke(CrabitUI::SettingsDestination destination, void* payload) noexcept {
        auto& self = *static_cast<Actions*>(payload);
        self.last = destination;
        ++self.calls[static_cast<unsigned>(destination)];
        if (destination == CrabitUI::SettingsDestination::gesture && self.navigator) {
            NanoUI::WidgetPtr route =
                NanoUI::makeOwned<DummyPage>(*self.allocator, NanoUI::AllocationKind::widget);
            const auto queued = self.navigator->push(route);
            return queued.status == NanoUI::NavigationStatus::queued;
        }
        return self.accept;
    }
};

class FaultAllocator final : public NanoUI::Allocator {
   public:
    void* allocate(std::size_t bytes, std::size_t alignment,
                   NanoUI::AllocationKind kind) noexcept override {
        ++calls;
        return calls == failAt ? nullptr : heap.allocate(bytes, alignment, kind);
    }
    void deallocate(void* pointer, std::size_t bytes, std::size_t alignment,
                    NanoUI::AllocationKind kind) noexcept override {
        heap.deallocate(pointer, bytes, alignment, kind);
    }
    NanoUI::Windows::HeapAllocator heap;
    std::size_t calls = 0, failAt = 0;
};

bool sameFile(const std::string& first, const std::string& second) {
    FILE* a = std::fopen(first.c_str(), "rb");
    FILE* b = std::fopen(second.c_str(), "rb");
    if (!a || !b) {
        if (a)
            std::fclose(a);
        if (b)
            std::fclose(b);
        return false;
    }
    bool equal = true;
    int ca, cb;
    do {
        ca = std::fgetc(a);
        cb = std::fgetc(b);
        if (ca != cb)
            equal = false;
    } while (equal && ca != EOF && cb != EOF);
    std::fclose(a);
    std::fclose(b);
    return equal;
}

void snapshot(const MemoryCanvas& source, unsigned height, const std::string& path) {
    NanoUI::RasterCanvas image(128, static_cast<int>(height));
    image.clear(0x000000FFu);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < 128; ++x)
            if (source.pixels[y * 128 + x])
                image.fillRect(NanoUI::Rect(static_cast<float>(x), static_cast<float>(y),
                                            static_cast<float>(x + 1), static_cast<float>(y + 1)),
                               0xFFFFFFFFu);
    CHECK(image.writePpm(path.c_str()));
}

std::string framePath(const char* output, unsigned id, unsigned height, const char* suffix) {
    return std::string(output) + "/" + NANOUI_LEGACY_NAME + "-settings-font" + std::to_string(id) +
           "-128x" + std::to_string(height) + "-" + suffix + ".ppm";
}

void draw(NanoUI::BuildOwner& owner, NanoUI::LegacyCanvasBridge<MemoryCanvas>& bridge,
          unsigned height) {
    CHECK(bridge.beginFrame());
    const auto frame = owner.frame(bridge, NanoUI::Size(128, static_cast<float>(height)));
    CHECK(frame.error == NanoUI::UiError::none && frame.painted);
    CHECK(bridge.balanced() && !bridge.error());
}

void renderAndExercise(const char* output, unsigned fontId, unsigned height) {
    MemoryCanvas memory;
    NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(memory);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    Actions actions;
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions(63, 512, 64));
        CrabitUI::SettingsHubModel model(owner, font, static_cast<float>(height), Actions::invoke,
                                         &actions);
        CHECK(model.selectedIndex() == -1 && model.scrollOffset() == 0);
        CHECK(model.viewportHeight() == static_cast<float>(height));
        const char* labels[] = {"DISPLAY", "POWER", "GESTURE", "CONNECT", "EXIT"};
        for (std::size_t i = 0; i < 5; ++i) {
            CHECK(std::strcmp(CrabitUI::SettingsHubModel::label(i), labels[i]) == 0);
            CHECK(CrabitUI::SettingsHubModel::rowKey(i) == NanoUI::Key::value(i + 1));
        }
        CHECK(std::strcmp(CrabitUI::SettingsHubModel::label(5), "") == 0);
        CHECK(CrabitUI::SettingsHubModel::count == 5);
        const auto rootResult = owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
            allocator, NanoUI::AllocationKind::widget, model));
        if (rootResult != NanoUI::UiError::none)
            std::printf("setRoot error=%u height=%u font=%u\n", static_cast<unsigned>(rootResult),
                        height, fontId);
        CHECK(rootResult == NanoUI::UiError::none);
        draw(owner, bridge, height);
        std::uint8_t heading[128 * 16];
        std::memcpy(heading, memory.pixels, sizeof heading);
        const auto firstFrame = framePath(output, fontId, height, "return");
        snapshot(memory, height, firstFrame);

        for (int i = 0; i < 5; ++i) {
            CHECK(owner.focusNext() == NanoUI::UiError::none);
            CHECK(model.selectedIndex() == i);
            CHECK(owner.dispatchFocused(NanoUI::InputAction::activate) == NanoUI::UiError::none);
            CHECK(actions.last == static_cast<CrabitUI::SettingsDestination>(i));
            CHECK(actions.calls[i] == 1);
            draw(owner, bridge, height);
            CHECK(std::memcmp(heading, memory.pixels, sizeof heading) == 0);
            const unsigned top = static_cast<unsigned>(16 + i * 20 - model.scrollOffset());
            CHECK(top >= 16 && top + 18 <= height);
            CHECK(memory.pixels[top * 128] && memory.pixels[(top + 17) * 128]);
            CHECK(memory.pixels[top * 128 + 127] && memory.pixels[(top + 17) * 128 + 127]);
            snapshot(memory, height,
                     framePath(output, fontId, height,
                               (std::string("row") + std::to_string(i)).c_str()));
        }
        CHECK(model.scrollOffset() > 0);
        CHECK(owner.focusNext() == NanoUI::UiError::none && model.selectedIndex() == 0);
        CHECK(owner.focusNext(false) == NanoUI::UiError::none && model.selectedIndex() == 4);
        CHECK(model.scrollOffset() > 0);
        CHECK(owner.dispatchFocused(NanoUI::InputAction::back) == NanoUI::UiError::none);
        CHECK(actions.last == CrabitUI::SettingsDestination::exit && actions.calls[4] == 2);
        draw(owner, bridge, height);
        snapshot(memory, height, framePath(output, fontId, height, "last"));
        CHECK(model.error() == NanoUI::UiError::none);
        CHECK(memory.clears == 0 && memory.displays == 0);
        std::printf("%s height=%u font=%u SettingsHubModel-size=%zu requested-peak-bytes=%zu\n",
                    NANOUI_LEGACY_NAME, height, fontId, sizeof(model),
                    allocator.statistics().peakBytes);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
}

void nullAction(unsigned fontId) {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions(63, 512, 64));
        CrabitUI::SettingsHubModel model(owner, font, 64, nullptr, nullptr);
        CHECK(owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
                  allocator, NanoUI::AllocationKind::widget, model)) == NanoUI::UiError::none);
        CHECK(owner.focusNext() == NanoUI::UiError::invalidInput);
        CHECK(model.selectedIndex() == -1);
        CHECK(owner.dispatchFocused(NanoUI::InputAction::back) == NanoUI::UiError::invalidInput);
        CHECK(model.error() == NanoUI::UiError::none);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
}

void callbackFailure(unsigned fontId) {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    Actions actions;
    actions.accept = false;
    {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions(63, 512, 64));
        CrabitUI::SettingsHubModel model(owner, font, 64, Actions::invoke, &actions);
        CHECK(owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
                  allocator, NanoUI::AllocationKind::widget, model)) == NanoUI::UiError::none);
        CHECK(owner.focusNext() == NanoUI::UiError::none);
        CHECK(owner.dispatchFocused(NanoUI::InputAction::activate) ==
              NanoUI::UiError::invalidInput);
        CHECK(model.error() == NanoUI::UiError::none);
        CHECK(owner.dispatchFocused(NanoUI::InputAction::back) == NanoUI::UiError::invalidInput);
        CHECK(actions.calls[4] == 1);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
}

void routeRestore(const char* output, unsigned fontId, unsigned height) {
    MemoryCanvas memory;
    NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(memory);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    Actions actions;
    {
        NanoUI::Navigator navigator(
            allocator, NanoUI::NavigatorOptions(16, 16, NanoUI::OwnerOptions(63, 512, 64)));
        NanoUI::WidgetPtr root =
            NanoUI::makeOwned<DummyPage>(allocator, NanoUI::AllocationKind::widget);
        CHECK(navigator.push(root).status == NanoUI::NavigationStatus::queued);
        CHECK(navigator.commit().status == NanoUI::NavigationStatus::complete);
        auto* owner = navigator.current();
        CHECK(owner != nullptr);
        {
            actions.navigator = &navigator;
            actions.allocator = &allocator;
            CrabitUI::SettingsHubModel model(*owner, font, static_cast<float>(height),
                                             Actions::invoke, &actions);
            CHECK(owner->setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
                      allocator, NanoUI::AllocationKind::widget, model)) == NanoUI::UiError::none);
            CHECK(owner->focusNext() == NanoUI::UiError::none);
            CHECK(owner->focusNext() == NanoUI::UiError::none);
            CHECK(owner->focusNext() == NanoUI::UiError::none);
            CHECK(model.selectedIndex() == 2);
            draw(*owner, bridge, height);
            const auto beforePixels = std::string(output) + "/" + NANOUI_LEGACY_NAME +
                                      "-settings-font" + std::to_string(fontId) + "-128x" +
                                      std::to_string(height) + "-route-before.ppm";
            snapshot(memory, height, beforePixels);
            const float beforeOffset = model.scrollOffset();
            const auto beforeFocus = owner->focus();
            std::uint8_t beforeRoute[128 * 96];
            std::memcpy(beforeRoute, memory.pixels, static_cast<std::size_t>(128 * height));

            CHECK(navigator.dispatchInput(NanoUI::InputAction::activate) == NanoUI::UiError::none);
            CHECK(navigator.pending() == 1);
            CHECK(navigator.commit().status == NanoUI::NavigationStatus::complete);
            CHECK(navigator.current() != owner);
            CHECK(navigator.dispatchInput(NanoUI::InputAction::next) ==
                  NanoUI::UiError::invalidInput);
            // Address the hidden Settings owner itself, not only the current
            // focus-less cover route. Neither path may invoke its callback.
            const int callsBeforeHiddenInput = actions.calls[2];
            CHECK(owner->focusNext() == NanoUI::UiError::invalidInput);
            CHECK(owner->dispatchInput(beforeFocus, NanoUI::InputAction::activate) ==
                  NanoUI::UiError::invalidInput);
            CHECK(owner->dispatchFocused(NanoUI::InputAction::activate) ==
                  NanoUI::UiError::invalidInput);
            CHECK(actions.calls[2] == callsBeforeHiddenInput && navigator.pending() == 0);
            draw(*navigator.current(), bridge, height);
            CHECK(std::memcmp(beforeRoute, memory.pixels, static_cast<std::size_t>(128 * height)) !=
                  0);
            snapshot(memory, height, framePath(output, fontId, height, "route-hidden"));
            CHECK(navigator.pop().status == NanoUI::NavigationStatus::queued);
            CHECK(navigator.commit().status == NanoUI::NavigationStatus::complete);
            CHECK(navigator.current() == owner);
            CHECK(owner->focus().serial == beforeFocus.serial);
            CHECK(model.selectedIndex() == 2 && model.scrollOffset() == beforeOffset);
            draw(*owner, bridge, height);
            const auto afterPixels = std::string(output) + "/" + NANOUI_LEGACY_NAME +
                                     "-settings-font" + std::to_string(fontId) + "-128x" +
                                     std::to_string(height) + "-route-after.ppm";
            snapshot(memory, height, afterPixels);
            CHECK(sameFile(beforePixels, afterPixels));
            CHECK(owner->clearRoot() ==
                  NanoUI::UiError::none);  // Remove borrows before model teardown.
        }
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    CHECK(memory.clears == 0 && memory.displays == 0);
}

enum class AllocationSweep { mount, focusRebuild };
void allocationSweep(unsigned fontId, AllocationSweep operation) {
    const bool mounting = operation == AllocationSweep::mount;
    // Measure the successful operation first, then fail every allocation it
    // performed. No frozen magic count that misses a later-added allocation.
    std::size_t positions = 0;
    for (std::size_t failure = 0; failure <= positions; ++failure) {
        MemoryCanvas memory;
        NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(memory);
        NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph,
                                      static_cast<std::uint8_t>(fontId));
        FaultAllocator fault;
        NanoUI::TrackingAllocator allocator(fault);
        Actions actions;
        {
            NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions(63, 512, 64));
            CrabitUI::SettingsHubModel model(owner, font, 64, Actions::invoke, &actions);
            std::uint8_t before[128 * 64]{};
            NanoUI::ElementHandle root;
            if (!mounting) {
                CHECK(owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
                          allocator, NanoUI::AllocationKind::widget, model)) ==
                      NanoUI::UiError::none);
                CHECK(owner.focusNext() == NanoUI::UiError::none);
                draw(owner, bridge, 64);
                std::memcpy(before, memory.pixels, sizeof before);
                root = owner.root();
                CHECK(owner.focusNext() == NanoUI::UiError::none);
            }
            const std::size_t start = fault.calls;
            fault.failAt = failure ? start + failure : 0;
            NanoUI::UiError error;
            bool painted = false;
            if (mounting) {
                NanoUI::WidgetPtr config = NanoUI::makeOwned<CrabitUI::SettingsHub>(
                    allocator, NanoUI::AllocationKind::widget, model);
                error = config ? owner.setRoot(std::move(config)) : NanoUI::UiError::outOfMemory;
            } else {
                const auto frame = owner.frame(bridge, NanoUI::Size(128, 64));
                error = frame.error;
                painted = frame.painted;
            }
            if (!failure) {
                CHECK(error == NanoUI::UiError::none);
                positions = fault.calls - start;
                CHECK(positions > 0);  // A zero-count sweep must not silently pass.
            } else {
                CHECK(error == NanoUI::UiError::outOfMemory && !painted);
                CHECK(allocator.statistics().failedAllocations > 0);
                CHECK(owner.root().serial == root.serial);
                CHECK(std::memcmp(before, memory.pixels, sizeof before) == 0);
            }
            fault.failAt = 0;
            if (mounting && failure) {
                CHECK(owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
                          allocator, NanoUI::AllocationKind::widget, model)) ==
                      NanoUI::UiError::none);
            }
            if (mounting)
                CHECK(owner.focusNext() == NanoUI::UiError::none);
            if (mounting || failure)
                draw(owner, bridge, 64);
            const int selected = mounting ? 0 : 1;
            CHECK(model.selectedIndex() == selected && model.error() == NanoUI::UiError::none);
            if (!mounting)
                CHECK(owner.root().serial == root.serial);
            CHECK(memory.pixels[(16 + 20 * selected) * 128] == 1);
            CHECK(memory.pixels[(33 + 20 * selected) * 128 + 127] == 1);
            const std::size_t callsBeforeCleanFrame = fault.calls;
            CHECK(!owner.frame(bridge, NanoUI::Size(128, 64)).painted);
            CHECK(fault.calls == callsBeforeCleanFrame);
            CHECK(owner.clearRoot() == NanoUI::UiError::none);
        }
        CHECK(allocator.statistics().currentBlocks() == 0);
        CHECK(memory.clears == 0 && memory.displays == 0);
    }
    std::printf("%s font=%u %s allocation failure positions=%zu\n", NANOUI_LEGACY_NAME, fontId,
                mounting ? "config+mount" : "focus rebuild", positions);
}

void insufficientBudget(unsigned fontId) {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    {
        NanoUI::BuildOwner owner(allocator);  // Defaults are deliberately unchanged.
        CrabitUI::SettingsHubModel model(owner, font, 64, nullptr, nullptr);
        CHECK(owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
                  allocator, NanoUI::AllocationKind::widget, model)) ==
              NanoUI::UiError::capacityExceeded);
        CHECK(owner.clearRoot() == NanoUI::UiError::none);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
}

void ownedRouteSession(const char* output, unsigned fontId) {
    MemoryCanvas memory;
    NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(memory);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    Actions actions;
    {
        NanoUI::Navigator nav(allocator,
                              NanoUI::NavigatorOptions(16, 16, NanoUI::OwnerOptions(63, 512, 64)));
        // No empty page or external SettingsHubModel bootstrap is needed.
        NanoUI::WidgetPtr config = NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
            allocator, NanoUI::AllocationKind::widget, font, 64, Actions::invoke, &actions);
        CHECK(nav.push(config).status == NanoUI::NavigationStatus::queued);
        CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
        auto* owner = nav.current();
        CHECK(owner != nullptr);
        draw(*owner, bridge, 64);
        for (int i = 0; i < 3; ++i)
            CHECK(owner->focusNext() == NanoUI::UiError::none);
        draw(*owner, bridge, 64);
        const auto root = owner->root();
        const auto focus = owner->focus();
        std::uint8_t before[128 * 64];
        std::memcpy(before, memory.pixels, sizeof before);
        const std::string sessionDirectory = std::string(output) + "/session";
        snapshot(memory, 64,
                 framePath(sessionDirectory.c_str(), fontId, 64, "state-owned-gesture"));
        CHECK(owner->dispatchFocused(NanoUI::InputAction::activate) == NanoUI::UiError::none);
        CHECK(actions.last == CrabitUI::SettingsDestination::gesture);
        // Same inputs retain State/model. Compatible root updates are queued;
        // incompatible session inputs fail in frame/build, preserving pixels.
        CHECK(owner->setRoot(NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
                  allocator, NanoUI::AllocationKind::widget, font, 64, Actions::invoke,
                  &actions)) == NanoUI::UiError::none);
        CHECK(owner->root().serial == root.serial && owner->focus().serial == focus.serial);
        CHECK(owner->setRoot(NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
                  allocator, NanoUI::AllocationKind::widget, font, 96, Actions::invoke,
                  &actions)) == NanoUI::UiError::none);
        const auto rejected = owner->frame(bridge, NanoUI::Size(128, 64));
        CHECK(rejected.error == NanoUI::UiError::invalidInput && !rejected.painted);
        CHECK(owner->root().serial == root.serial && owner->focus().serial == focus.serial);
        CHECK(std::memcmp(before, memory.pixels, sizeof before) == 0);
        CHECK(owner->setRoot(NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
                  allocator, NanoUI::AllocationKind::widget, font, 64, Actions::invoke,
                  &actions)) == NanoUI::UiError::none);
        draw(*owner, bridge, 64);
        CHECK(std::memcmp(before, memory.pixels, sizeof before) == 0);
        NanoUI::WidgetPtr cover =
            NanoUI::makeOwned<DummyPage>(allocator, NanoUI::AllocationKind::widget);
        CHECK(nav.push(cover).status == NanoUI::NavigationStatus::queued);
        CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
        draw(*nav.current(), bridge, 64);
        CHECK(std::memcmp(before, memory.pixels, sizeof before) != 0);
        CHECK(owner->dispatchInput(focus, NanoUI::InputAction::activate) ==
              NanoUI::UiError::invalidInput);
        CHECK(nav.pop().status == NanoUI::NavigationStatus::queued);
        CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
        CHECK(nav.current() == owner && owner->focus().serial == focus.serial);
        draw(*owner, bridge, 64);
        CHECK(std::memcmp(before, memory.pixels, sizeof before) == 0);
        snapshot(memory, 64,
                 framePath(sessionDirectory.c_str(), fontId, 64, "state-owned-returned"));
        CHECK(owner->setRoot(NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
                  allocator, NanoUI::AllocationKind::widget, font, 96, Actions::invoke, &actions,
                  NanoUI::Key::value(10))) == NanoUI::UiError::none);
        CHECK(owner->root().serial != root.serial && owner->focus().serial == 0);
        draw(*owner, bridge, 96);
        CHECK(owner->focusNext() == NanoUI::UiError::none);
        draw(*owner, bridge, 96);
        CHECK(owner->dispatchFocused(NanoUI::InputAction::activate) == NanoUI::UiError::none);
        CHECK(actions.last == CrabitUI::SettingsDestination::display);
        // Navigator destruction owns session/model teardown; no manual clearRoot.
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
    CHECK(memory.clears == 0 && memory.displays == 0);
}

void ownedRouteAllocationSweep(unsigned fontId) {
    std::size_t positions = 0;
    for (std::size_t failure = 0; failure <= positions; ++failure) {
        MemoryCanvas memory;
        NanoUI::LegacyCanvasBridge<MemoryCanvas> bridge(memory);
        NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph,
                                      static_cast<std::uint8_t>(fontId));
        FaultAllocator fault;
        NanoUI::TrackingAllocator allocator(fault);
        Actions actions;
        {
            NanoUI::Navigator nav(
                allocator, NanoUI::NavigatorOptions(16, 16, NanoUI::OwnerOptions(63, 512, 64)));
            NanoUI::WidgetPtr anchor =
                NanoUI::makeOwned<DummyPage>(allocator, NanoUI::AllocationKind::widget);
            CHECK(nav.push(anchor).status == NanoUI::NavigationStatus::queued);
            CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
            auto* oldOwner = nav.current();
            const auto oldRoot = oldOwner->root();
            draw(*oldOwner, bridge, 64);
            std::uint8_t before[128 * 64];
            std::memcpy(before, memory.pixels, sizeof before);
            const auto oldBytes = allocator.statistics().currentBytes();
            const auto start = fault.calls;
            fault.failAt = failure ? start + failure : 0;
            NanoUI::UiError error;
            {
                NanoUI::WidgetPtr route = NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
                    allocator, NanoUI::AllocationKind::widget, font, 64, Actions::invoke, &actions);
                if (!route)
                    error = NanoUI::UiError::outOfMemory;
                else {
                    const auto request = nav.push(route);
                    error = request.status == NanoUI::NavigationStatus::queued ? nav.commit().error
                                                                               : request.error;
                }
            }  // Rejected push retains the config; release it before accounting.
            if (!failure) {
                CHECK(error == NanoUI::UiError::none);
                positions = fault.calls - start;
                CHECK(positions > 0);
            } else {
                CHECK(error == NanoUI::UiError::outOfMemory);
                CHECK(nav.depth() == 1 && nav.pending() == 0 && nav.current() == oldOwner);
                CHECK(oldOwner->root().serial == oldRoot.serial);
                CHECK(allocator.statistics().currentBytes() == oldBytes);
                CHECK(std::memcmp(before, memory.pixels, sizeof before) == 0);
            }
            fault.failAt = 0;
            if (failure) {
                NanoUI::WidgetPtr retry = NanoUI::makeOwned<CrabitUI::SettingsHubRoute>(
                    allocator, NanoUI::AllocationKind::widget, font, 64, Actions::invoke, &actions);
                CHECK(nav.push(retry).status == NanoUI::NavigationStatus::queued);
                CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
            }
            CHECK(nav.depth() == 2 && nav.current() != oldOwner);
            CHECK(nav.current()->focusNext() == NanoUI::UiError::none);
            draw(*nav.current(), bridge, 64);
            CHECK(nav.dispatchInput(NanoUI::InputAction::activate) == NanoUI::UiError::none);
            CHECK(actions.last == CrabitUI::SettingsDestination::display);
            CHECK(nav.pop().status == NanoUI::NavigationStatus::queued);
            CHECK(nav.commit().status == NanoUI::NavigationStatus::complete);
            CHECK(nav.current() == oldOwner && allocator.statistics().currentBytes() == oldBytes);
        }
        CHECK(allocator.statistics().currentBlocks() == 0);
    }
    std::printf("%s font=%u route factory/queue/mount allocation failure positions=%zu\n",
                NANOUI_LEGACY_NAME, fontId, positions);
}

void invalidHeights(unsigned fontId) {
    NanoUI::Windows::HeapAllocator heap;
    NanoUI::TrackingAllocator allocator(heap);
    NanoUI::LegacyBitmapFont font(font5x7, get_chinese_glyph, static_cast<std::uint8_t>(fontId));
    const float values[] = {33.0f, 32768.0f, std::numeric_limits<float>::infinity(),
                            -std::numeric_limits<float>::infinity(),
                            std::numeric_limits<float>::quiet_NaN()};
    for (float h : values) {
        NanoUI::BuildOwner owner(allocator, NanoUI::OwnerOptions(63, 512, 64));
        CrabitUI::SettingsHubModel model(owner, font, h, nullptr, nullptr);
        const auto result = owner.setRoot(NanoUI::makeOwned<CrabitUI::SettingsHub>(
            allocator, NanoUI::AllocationKind::widget, model));
        CHECK(result == NanoUI::UiError::invalidInput);
    }
    CHECK(allocator.statistics().currentBlocks() == 0);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    for (unsigned fontId : {0u, 1u}) {
        renderAndExercise(argv[1], fontId, 64);
        renderAndExercise(argv[1], fontId, 96);
        nullAction(fontId);
        callbackFailure(fontId);
        allocationSweep(fontId, AllocationSweep::mount);
        allocationSweep(fontId, AllocationSweep::focusRebuild);
        ownedRouteSession(argv[1], fontId);
        ownedRouteAllocationSweep(fontId);
        insufficientBudget(fontId);
        invalidHeights(fontId);
        routeRestore(argv[1], fontId, 64);
        routeRestore(argv[1], fontId, 96);
    }
    std::printf("%s settings hub: %d checks, %d failures\n", NANOUI_LEGACY_NAME, checks, failures);
    return failures ? 1 : 0;
}
