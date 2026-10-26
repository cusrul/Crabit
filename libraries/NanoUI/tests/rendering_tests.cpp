#include <NanoUI/rendering/render_object.h>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <utility>

namespace {
using namespace NanoUI;
int checks = 0, failures = 0;
#define CHECK(e)                                                \
    do {                                                        \
        ++checks;                                               \
        if (!(e)) {                                             \
            ++failures;                                         \
            std::printf("%s:%d: %s\n", __FILE__, __LINE__, #e); \
        }                                                       \
    } while (false)

// Fixed arena tests all object allocation through Allocator, without OS calls.
class Arena final : public Allocator {
   public:
    void* allocate(std::size_t bytes, std::size_t alignment,
                   AllocationKind kind) noexcept override {
        CHECK(kind == AllocationKind::renderObject);
        if (fail || bytes > sizeof(slots[0]) || alignment > alignof(Slot))
            return nullptr;
        for (std::size_t i = 0; i < 128; ++i)
            if (!used[i]) {
                used[i] = true;
                ++live;
                return &slots[i];
            }
        return nullptr;
    }
    void deallocate(void* p, std::size_t, std::size_t, AllocationKind kind) noexcept override {
        CHECK(kind == AllocationKind::renderObject);
        for (std::size_t i = 0; i < 128; ++i)
            if (p == &slots[i]) {
                CHECK(used[i]);
                used[i] = false;
                --live;
                return;
            }
        CHECK(false);
    }
    bool fail = false;
    int live = 0;

   private:
    struct alignas(std::max_align_t) Slot {
        unsigned char bytes[512];
    };
    Slot slots[128];
    bool used[128]{};
};

class Box : public RenderObject {
   public:
    explicit Box(Size desired = Size(20, 20), int* destroyed = nullptr) noexcept
        : desired(desired), destroyed(destroyed) {}
    ~Box() noexcept override {
        if (destroyed)
            ++*destroyed;
    }
    Size desired;
    int* destroyed;
    int layouts = 0;
    bool interactive = true;
    bool invalidateLayout = false, invalidatePaint = false;

   protected:
    Size performLayout(const BoxConstraints& c) noexcept override {
        ++layouts;
        RenderObject::performLayout(c);
        if (invalidateLayout)
            markNeedsLayout();
        return desired;
    }
    void performPaint(PaintCanvas& canvas) noexcept override {
        canvas.fillRect(Rect::fromSize(size()), 7);
        if (invalidatePaint)
            markNeedsPaint();
    }
    bool hitTestSelf(Offset) const noexcept override { return interactive; }
};

class Canvas final : public PaintCanvas {
   public:
    struct State {
        Offset offset;
        Rect clip = Rect(-1000, -1000, 1000, 1000);
    };
    State state;
    State stack[RenderObject::maxDepth];
    Rect clips[128];
    Offset offsets[128];
    int depth = 0, peak = 0, saves = 0, restores = 0, fills = 0;
    void save() noexcept override {
        CHECK(depth < static_cast<int>(RenderObject::maxDepth));
        stack[depth++] = state;
        ++saves;
        if (depth > peak)
            peak = depth;
    }
    void restore() noexcept override {
        CHECK(depth > 0);
        state = stack[--depth];
        ++restores;
    }
    void translate(Offset offset) noexcept override { state.offset = state.offset + offset; }
    void clipRect(Rect r) noexcept override {
        r.left += state.offset.dx;
        r.right += state.offset.dx;
        r.top += state.offset.dy;
        r.bottom += state.offset.dy;
        state.clip = state.clip.intersect(r);
    }
    void fillRect(Rect, std::uint32_t color) noexcept override {
        CHECK(color == 7);
        CHECK(fills < 128);
        clips[fills] = state.clip;
        offsets[fills] = state.offset;
        ++fills;
    }
};

void constraintsAndCoordinates() {
    BoxConstraints normalized(1, 2, 3, 4);
    CHECK(BoxConstraints(-5, -1, 9, 2).normalize(normalized));
    CHECK(normalized.minWidth == 0 && normalized.maxWidth == 0);
    CHECK(normalized.minHeight == 9 && normalized.maxHeight == 9);
    const auto saved = normalized;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(!BoxConstraints(nan, 10, 0, 10).normalize(normalized));
    CHECK(!BoxConstraints(0, nan, 0, 10).normalize(normalized));
    CHECK(!BoxConstraints(inf, inf, 0, 10).normalize(normalized));
    CHECK(!BoxConstraints(0, -inf, 0, 10).normalize(normalized));
    CHECK(normalized == saved);
    CHECK(BoxConstraints().isNormalized());
    Box box(Size(100, -10));
    CHECK(!box.layout(BoxConstraints(10, 2, 0, 10)));
    CHECK(box.layouts == 0 && box.needsLayout());
    CHECK(box.layout(BoxConstraints(5, 30, 7, 20)));
    CHECK(box.size().width == 30 && box.size().height == 7);
    CHECK(box.layout(BoxConstraints(5, 30, 7, 20)) && box.layouts == 1);
    CHECK(!box.layout(BoxConstraints(0, nan, 0, 20)));
    CHECK(box.size().width == 30 && box.layouts == 1);
    box.desired = Size(nan, inf);
    box.markNeedsLayout();
    CHECK(box.layout(BoxConstraints(5, 30, 7, 20)));
    CHECK(box.size().width == 5 && box.size().height == 7);
    CHECK(box.setOffset(Offset(-2, 3)));
    CHECK(!box.setOffset(Offset(inf, 0)) && box.offset().dx == -2);
    CHECK(!box.setOffset(Offset(0, nan)));
    const Offset delta = Offset(8, 9) - Offset(2, 3);
    CHECK(delta.dx == 6 && delta.dy == 6);
    CHECK(Rect(0, 0, 10, 10).contains(Offset(0, 0)));
    CHECK(!Rect(0, 0, 10, 10).contains(Offset(10, 2)));
    CHECK(!Rect(0, 0, 10, 10).contains(Offset(nan, 2)));
    const Rect empty = Rect(0, 0, 1, 1).intersect(Rect(2, 2, 3, 3));
    CHECK(empty.left == empty.right && empty.top == empty.bottom);
}

void ownershipAndBounds() {
    Arena arena;
    int destroyed = 0;
    auto root = makeOwned<Box>(arena, AllocationKind::renderObject, Size(50, 50), &destroyed);
    auto a = makeOwned<Box>(arena, AllocationKind::renderObject, Size(10, 10), &destroyed);
    auto b = makeOwned<Box>(arena, AllocationKind::renderObject, Size(10, 10), &destroyed);
    auto c = makeOwned<Box>(arena, AllocationKind::renderObject, Size(10, 10), &destroyed);
    auto* ap = a.get();
    auto* bp = b.get();
    auto* cp = c.get();
    CHECK(!root->append(root) && root);
    CHECK(root->append(a) && !a);
    CHECK(root->append(b) && !b);
    CHECK(root->append(c) && !c);
    CHECK(!root->append(a));           // Consumed owner cannot double-parent a child.
    CHECK(!bp->append(root) && root);  // Cycle rejection preserves root's owner.
    CHECK(root->childCount() == 3 && root->firstChild() == ap && root->lastChild() == cp);
    CHECK(ap->nextSibling() == bp && bp->previousSibling() == ap);
    Box stranger;
    CHECK(!root->detach(stranger));
    auto middle = root->detach(*bp);
    CHECK(middle.get() == bp && !bp->parent() && !bp->nextSibling() && !bp->previousSibling());
    CHECK(ap->nextSibling() == cp && cp->previousSibling() == ap && root->childCount() == 2);
    CHECK(root->append(middle) && !middle && root->lastChild() == bp);
    auto first = root->detach(*ap);
    CHECK(root->firstChild() == cp && !cp->previousSibling());
    auto last = root->detach(*bp);
    CHECK(root->lastChild() == cp && !cp->nextSibling());
    root.reset();
    CHECK(destroyed == 2 && arena.live == 2);
    first.reset();
    last.reset();
    CHECK(destroyed == 4 && arena.live == 0);
    arena.fail = true;
    auto failed = makeOwned<Box>(arena, AllocationKind::renderObject);
    CHECK(!failed && arena.live == 0);
    arena.fail = false;
    auto chain = makeOwned<Box>(arena, AllocationKind::renderObject);
    RenderObject* tail = chain.get();
    for (std::size_t i = 1; i < RenderObject::maxDepth; ++i) {
        auto next = makeOwned<Box>(arena, AllocationKind::renderObject);
        auto* raw = next.get();
        CHECK(tail->append(next));
        tail = raw;
    }
    auto excess = makeOwned<Box>(arena, AllocationKind::renderObject);
    CHECK(!tail->append(excess) && excess && !excess->parent());
    // Whole detached subtrees, not just their roots, participate in depth bound.
    CHECK(!excess->append(chain) && chain);
    CHECK(chain->layout(BoxConstraints(0, 100, 0, 100)));
    Canvas boundedCanvas;
    chain->paint(boundedCanvas);
    CHECK(boundedCanvas.peak == static_cast<int>(RenderObject::maxDepth));
    CHECK(boundedCanvas.depth == 0 && boundedCanvas.saves == boundedCanvas.restores);
    CHECK(chain->hitTest(Offset(1, 1)).target == tail);
    chain.reset();
    excess.reset();
    CHECK(arena.live == 0);
    auto wide = makeOwned<Box>(arena, AllocationKind::renderObject);
    for (int i = 0; i < 100; ++i) {
        auto child = makeOwned<Box>(arena, AllocationKind::renderObject);
        CHECK(wide->append(child));
    }
    wide.reset();
    CHECK(arena.live == 0);
}

void paintDirtyAndHit() {
    Arena arena;
    auto root = makeOwned<Box>(arena, AllocationKind::renderObject, Size(30, 30));
    auto back = makeOwned<Box>(arena, AllocationKind::renderObject, Size(20, 20));
    auto front = makeOwned<Box>(arena, AllocationKind::renderObject, Size(20, 20));
    auto deep = makeOwned<Box>(arena, AllocationKind::renderObject, Size(10, 10));
    auto* bp = back.get();
    auto* fp = front.get();
    auto* dp = deep.get();
    CHECK(root->setOffset(Offset(3, 4)));
    CHECK(front->setOffset(Offset(15, 15)));
    CHECK(deep->setOffset(Offset(2, 2)));
    CHECK(front->append(deep));
    CHECK(root->append(back));
    CHECK(root->append(front));
    const Offset global = dp->localToGlobal(Offset(1, 1));
    CHECK(global.dx == 21 && global.dy == 22);
    const Offset local = dp->globalToLocal(global);
    CHECK(local.dx == 1 && local.dy == 1);
    CHECK(!root->hitTest(Offset(1, 1)));  // Layout must precede hit testing.
    CHECK(root->layout(BoxConstraints(0, 100, 0, 100)));
    Canvas canvas;
    root->paint(canvas);
    CHECK(canvas.saves == 4 && canvas.restores == 4 && canvas.depth == 0 && canvas.peak == 3);
    CHECK(canvas.fills == 4 && canvas.offsets[2].dx == 18 && canvas.offsets[3].dx == 20);
    CHECK(canvas.clips[2].left == 18 && canvas.clips[2].right == 33);
    CHECK(canvas.clips[3].left == 20 && canvas.clips[3].right == 30);
    CHECK(canvas.state.offset.dx == 0 && canvas.state.clip.left == -1000);
    CHECK(!root->needsPaint() && !fp->needsPaint() && !dp->needsPaint());
    auto hit = root->hitTest(Offset(18, 18));
    CHECK(hit.target == dp && hit.localPosition.dx == 1 && hit.localPosition.dy == 1);
    dp->interactive = false;
    CHECK(root->hitTest(Offset(18, 18)).target == fp);
    fp->interactive = false;
    CHECK(root->hitTest(Offset(18, 18)).target == bp);
    CHECK(!root->hitTest(Offset(30, 18)));  // Ancestor clip applies to hits too.
    dp->markNeedsPaint();
    CHECK(dp->needsPaint() && fp->needsPaint() && root->needsPaint() && !bp->needsPaint());
    root->paint(canvas);
    CHECK(!root->needsPaint());
    CHECK(dp->setOffset(Offset(3, 3)) && root->needsPaint() && !root->needsLayout());
    dp->markNeedsLayout();
    CHECK(dp->needsLayout() && fp->needsLayout() && root->needsLayout() && !bp->needsLayout());
    const int oldFills = canvas.fills;
    root->paint(canvas);
    CHECK(canvas.fills == oldFills);
    CHECK(root->layout(BoxConstraints(0, 100, 0, 100)));
    CHECK(bp->layouts == 1 && fp->layouts == 2 && dp->layouts == 2);
    dp->invalidatePaint = true;
    root->paint(canvas);
    CHECK(root->needsPaint() && fp->needsPaint() && dp->needsPaint());
    dp->invalidatePaint = false;
    dp->invalidateLayout = true;
    dp->markNeedsLayout();
    CHECK(root->layout(BoxConstraints(0, 100, 0, 100)));
    CHECK(root->needsLayout() && dp->needsLayout());
    dp->invalidateLayout = false;
    CHECK(root->layout(BoxConstraints(0, 100, 0, 100)));
    auto detached = root->detach(*fp);
    CHECK(detached && root->needsLayout());
    CHECK(root->layout(BoxConstraints(0, 100, 0, 100)));
    CHECK(root->hitTest(Offset(18, 18)).target == bp);
    root.reset();
    detached.reset();
    CHECK(arena.live == 0);
}
}  // namespace

int main() {
    constraintsAndCoordinates();
    ownershipAndBounds();
    paintDirtyAndHit();
    std::printf("rendering: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
