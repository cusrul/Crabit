#include <NanoUI/framework/build_owner.h>
#include <NanoUI/framework/build_context.h>
#include <NanoUI/framework/build_result.h>
#include <NanoUI/framework/widget.h>
#include <NanoUI/navigation/navigator.h>
#include <NanoUI/rendering/render_object.h>
#include <NanoUI/widgets/layout.h>
#include <windows/heap_allocator.h>
#include <windows/raster_canvas.h>
#include <cstdio>
#include <limits>

namespace {
using namespace NanoUI;
int checks = 0, failures = 0;
#define CHECK(e)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(e)) {                                \
            ++failures;                            \
            std::printf("%d: %s\n", __LINE__, #e); \
        }                                          \
    } while (false)
class Faults final : public Allocator {
   public:
    void* allocate(std::size_t bytes, std::size_t alignment,
                   AllocationKind kind) noexcept override {
        ++calls;
        return calls == failAt ? nullptr : heap.allocate(bytes, alignment, kind);
    }
    void deallocate(void* p, std::size_t bytes, std::size_t alignment,
                    AllocationKind kind) noexcept override {
        heap.deallocate(p, bytes, alignment, kind);
    }
    int calls = 0, failAt = 0;
    Windows::HeapAllocator heap;
};
struct Probe {
    int created = 0, destroyed = 0, painted = 0, activated = 0;
    BuildOwner* owner = nullptr;
    Navigator* navigator = nullptr;
    UiError paintMutation = UiError::none, inputMutation = UiError::none;
    NavigationStatus paintNavigation = NavigationStatus::complete,
                     inputNavigation = NavigationStatus::complete;
};
class Box final : public RenderObject {
   public:
    explicit Box(Probe& p) noexcept : probe(p) { ++probe.created; }
    ~Box() noexcept override { ++probe.destroyed; }
    bool handleInput(InputAction) noexcept override {
        ++probe.activated;
        if (probe.owner) {
            probe.inputMutation = probe.owner->clearRoot();
            CHECK(probe.owner->requestBuild(probe.owner->root()) == UiError::none);
        }
        if (probe.navigator)
            probe.inputNavigation = probe.navigator->commit().status;
        return true;
    }
    std::uint32_t color = 0xFFFFFFFFu;

   protected:
    Size performLayout(const BoxConstraints&) noexcept override { return Size(8, 8); }
    void performPaint(PaintCanvas& canvas) noexcept override {
        ++probe.painted;
        canvas.fillRect(Rect::fromSize(size()), color);
        if (probe.owner)
            probe.paintMutation = probe.owner->clearRoot();
        if (probe.navigator)
            probe.paintNavigation = probe.navigator->commit().status;
    }

   private:
    Probe& probe;
};
class BoxWidget final : public RenderWidget {
   public:
    BoxWidget(Probe& p, std::uint32_t color = 0xFFFFFFFFu, Key key = {}) noexcept
        : RenderWidget(typeId<BoxWidget>(), key), probe(p), color(color) {}
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<Box>(a, AllocationKind::renderObject, probe);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        static_cast<Box&>(r).color = color;
        r.markNeedsPaint();
        return UiError::none;
    }
    Probe& probe;
    std::uint32_t color;
};
class GroupWidget final : public RenderWidget {
   public:
    GroupWidget(Probe& p, bool reverse) noexcept
        : RenderWidget(typeId<GroupWidget>()), probe(p), reverse(reverse) {}
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderObject>(a, AllocationKind::renderObject);
    }
    UiError updateRenderObject(RenderObject&) const noexcept override { return UiError::none; }
    BuildResult build(BuildContext& c) const noexcept override {
        BuildResult result;
        for (int i = 0; i < 2; ++i) {
            const int index = reverse ? 1 - i : i;
            WidgetPtr child = makeOwned<BoxWidget>(c.allocator(), AllocationKind::widget, probe,
                                                   index == 0 ? 0xFFFFFFFFu : 0x000000FFu,
                                                   Key::value(static_cast<unsigned>(index + 1)));
            if (!child) {
                result.error = UiError::outOfMemory;
                return result;
            }
            result.error = result.children.append(c.allocator(), child);
            if (result.error != UiError::none)
                return result;
        }
        return result;
    }
    Probe& probe;
    bool reverse;
};
void keyedRenderReorder() {
    Faults upstream;
    TrackingAllocator a(upstream);
    Probe p;
    {
        BuildOwner owner(a);
        auto config = makeOwned<GroupWidget>(a, AllocationKind::widget, p, false);
        CHECK(owner.setRoot(std::move(config)) == UiError::none);
        const auto root = owner.root();
        const auto first = owner.childAt(root, 0), second = owner.childAt(root, 1);
        RasterCanvas canvas(8, 8);
        CHECK(owner.frame(canvas, Size(8, 8)).painted && canvas.pixel(1, 1) == 0x000000FFu);
        config = makeOwned<GroupWidget>(a, AllocationKind::widget, p, true);
        CHECK(owner.setRoot(std::move(config)) == UiError::none);
        CHECK(owner.frame(canvas, Size(8, 8)).painted && canvas.pixel(1, 1) == 0xFFFFFFFFu);
        CHECK(owner.childAt(root, 0).serial == second.serial &&
              owner.childAt(root, 1).serial == first.serial);
        CHECK(p.created == 2 && p.destroyed == 0);
        config = makeOwned<GroupWidget>(a, AllocationKind::widget, p, false);
        CHECK(owner.setRoot(std::move(config)) == UiError::none);
        upstream.failAt = upstream.calls + 1;
        const auto failed = owner.frame(canvas, Size(8, 8));
        CHECK(failed.error == UiError::outOfMemory && !failed.painted);
        CHECK(canvas.pixel(1, 1) == 0xFFFFFFFFu && owner.childAt(root, 0).serial == second.serial);
        upstream.failAt = 0;
        CHECK(owner.frame(canvas, Size(8, 8)).painted && canvas.pixel(1, 1) == 0x000000FFu);
        CHECK(p.created == 2 && p.destroyed == 0);
    }
    CHECK(p.created == p.destroyed && a.statistics().currentBlocks() == 0);
}
void identityAndCallbackSafety() {
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    Probe p;
    RasterCanvas canvas(8, 8);
    {
        BuildOwner owner(a);
        p.owner = &owner;
        auto widget = makeOwned<BoxWidget>(a, AllocationKind::widget, p);
        CHECK(owner.setRoot(std::move(widget)) == UiError::none);
        const auto handle = owner.root();
        CHECK(owner.frame(canvas, Size(8, 8)).painted);
        CHECK(p.created == 1 && p.paintMutation == UiError::invalidPhase);
        CHECK(canvas.pixel(1, 1) == 0xFFFFFFFFu && canvas.balanced());
        CHECK(!owner.frame(canvas, Size(8, 8)).painted &&
              p.painted == 1);  // A clean frame does no work.
        auto update = makeOwned<BoxWidget>(a, AllocationKind::widget, p, 0x000000FFu);
        CHECK(owner.setRoot(std::move(update)) == UiError::none);
        CHECK(owner.frame(canvas, Size(8, 8)).painted);
        CHECK(owner.root().serial == handle.serial && p.created == 1 && p.destroyed == 0);
        CHECK(canvas.pixel(1, 1) == 0x000000FFu);
        CHECK(owner.dispatchInput(handle, InputAction::activate) == UiError::none);
        CHECK(p.activated == 1 && p.inputMutation == UiError::invalidPhase);
        CHECK(owner.frame(canvas, Size(8, 8)).painted && p.created == 1);
        CHECK(owner.setRootActive(false) == UiError::none);
        CHECK(!owner.frame(canvas, Size(8, 8)).painted);
        CHECK(owner.dispatchInput(handle, InputAction::activate) == UiError::invalidInput);
        CHECK(owner.frame(canvas, Size(-1, 8)).error == UiError::invalidInput);
        CHECK(owner.frame(canvas, Size(std::numeric_limits<float>::infinity(), 8)).error ==
              UiError::invalidInput);
        CHECK(owner.clearRoot() == UiError::none);
        CHECK(owner.dispatchInput(handle, InputAction::activate) == UiError::staleHandle);
    }
    CHECK(p.created == p.destroyed && a.statistics().currentBlocks() == 0);
}
void stagingFailuresAndBorrowedOwnership() {
    for (int fail = 1; fail <= 8; ++fail) {
        Faults upstream;
        TrackingAllocator a(upstream);
        Probe p;
        {
            BuildOwner owner(a);
            auto config =
                makeOwned<BoxWidget>(a, AllocationKind::widget, p, 0xFFFFFFFFu, Key::value(1));
            CHECK(owner.setRoot(std::move(config)) == UiError::none);
            const auto old = owner.root();
            config = makeOwned<BoxWidget>(a, AllocationKind::widget, p, 0x000000FFu, Key::value(2));
            upstream.failAt = upstream.calls + fail;
            const auto result = owner.setRoot(std::move(config));
            CHECK(result == UiError::none || result == UiError::outOfMemory);
            if (result != UiError::none)
                CHECK(owner.root().serial == old.serial);
            upstream.failAt = 0;
            RasterCanvas canvas(8, 8);
            CHECK(owner.frame(canvas, Size(8, 8)).painted && !canvas.error());
        }
        CHECK(p.created == p.destroyed && a.statistics().currentBlocks() == 0);
    }
    Probe p;
    {
        Box parent(p);
        {
            Box child(p);
            CHECK(parent.attachBorrowed(child));
            CHECK(!parent.attachBorrowed(child));
            CHECK(parent.firstChild() == &child && child.parent() == &parent);
        }
        CHECK(parent.childCount() == 0 && parent.firstChild() == nullptr);
    }
    CHECK(p.created == p.destroyed);
}
void navigationSafePoint() {
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    Probe p;
    {
        Navigator navigator(a);
        p.navigator = &navigator;
        WidgetPtr widget = makeOwned<BoxWidget>(a, AllocationKind::widget, p);
        CHECK(navigator.push(widget).status == NavigationStatus::queued);
        CHECK(navigator.commit().status == NavigationStatus::complete);
        p.owner = navigator.current();
        CHECK(navigator.pop().status == NavigationStatus::queued);
        RasterCanvas canvas(8, 8);
        CHECK(p.owner->frame(canvas, Size(8, 8)).painted);
        CHECK(p.paintNavigation == NavigationStatus::busy && navigator.pending() == 1);
        CHECK(p.owner->dispatchInput(p.owner->root(), InputAction::activate) == UiError::none);
        CHECK(p.inputNavigation == NavigationStatus::busy && navigator.depth() == 1);
        CHECK(navigator.commit().status == NavigationStatus::atRoot);
    }
    CHECK(p.created == p.destroyed && a.statistics().currentBlocks() == 0);
}
void routeRevealRepaints() {
    Windows::HeapAllocator heap;
    TrackingAllocator a(heap);
    Probe p;
    {
        Navigator nav(a);
        RasterCanvas canvas(8, 8);
        WidgetPtr first = makeOwned<BoxWidget>(a, AllocationKind::widget, p, 0xFFFFFFFFu);
        CHECK(nav.push(first).status == NavigationStatus::queued);
        CHECK(nav.commit().status == NavigationStatus::complete);
        CHECK(nav.current()->frame(canvas, Size(8, 8)).painted);
        CHECK(canvas.pixel(1, 1) == 0xFFFFFFFFu);
        CHECK(!nav.current()->frame(canvas, Size(8, 8)).painted);
        for (int i = 0; i < 3; ++i) {
            WidgetPtr second = makeOwned<BoxWidget>(a, AllocationKind::widget, p, 0x000000FFu);
            CHECK(nav.push(second).status == NavigationStatus::queued);
            CHECK(nav.commit().status == NavigationStatus::complete);
            CHECK(nav.current()->frame(canvas, Size(8, 8)).painted);
            CHECK(canvas.pixel(1, 1) == 0x000000FFu);
            CHECK(nav.pop().status == NavigationStatus::queued);
            CHECK(nav.commit().status == NavigationStatus::complete);
            CHECK(nav.current()->frame(canvas, Size(8, 8)).painted);
            CHECK(canvas.pixel(1, 1) == 0xFFFFFFFFu);
            CHECK(!nav.current()->frame(canvas, Size(8, 8)).painted);
        }
    }
    CHECK(p.created == p.destroyed && a.statistics().currentBlocks() == 0);
}
void paddingPreservesConstraints() {
    Probe p;
    Box child(p);
    RenderPadding padding;
    padding.configure(Insets(1));
    CHECK(padding.attachBorrowed(child));
    padding.layout(BoxConstraints::tight(Size(20, 14)));
    CHECK(child.size().width == 18 && child.size().height == 12);
    RasterCanvas canvas(20, 14);
    canvas.clear(0x000000FFu);
    padding.paint(canvas);
    CHECK(canvas.pixel(17, 11) == 0xFFFFFFFFu);
    CHECK(canvas.pixel(0, 0) == 0x000000FFu);
    padding.layout(BoxConstraints::tight(Size(1, 1)));
    CHECK(child.size().width == 0 && child.size().height == 0);
}
}  // namespace
int main() {
    identityAndCallbackSafety();
    stagingFailuresAndBorrowedOwnership();
    navigationSafePoint();
    keyedRenderReorder();
    routeRevealRepaints();
    paddingPreservesConstraints();
    std::printf("frame pipeline: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
