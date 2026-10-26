// Contract tests for the bounded core layouts added in the Windows layout
// integration. This intentionally targets the original RenderObject tree.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
#define CHECK(expr)                                        \
    do {                                                   \
        if (!(expr)) {                                     \
            ++failures;                                    \
            std::printf("line %d: %s\n", __LINE__, #expr); \
        }                                                  \
    } while (false)

class HeapAdapter final : public NanoUI::MemoryAdapter {
   public:
    size_t allocations = 0;
    size_t releases = 0;
    void* alloc(size_t bytes) override {
        void* p = std::malloc(bytes);
        if (p)
            ++allocations;
        return p;
    }
    void free(void* p) override {
        if (p) {
            ++releases;
            std::free(p);
        }
    }
};

class TestCanvas final : public Canvas {
   public:
    enum { W = 40, H = 32 };
    uint8_t pixels[W * H]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return W; }
    int16_t height() override { return H; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && y >= 0 && x < W && y < H) {
            uint8_t& dst = pixels[y * W + x];
            if (color == C_INVERT)
                dst = !dst;
            else
                dst = color == C_WHITE;
        }
    }
    uint8_t at(int x, int y) const { return pixels[y * W + x]; }
};

class FixedRender final : public RenderObject {
   public:
    int16_t naturalWidth, naturalHeight;
    uint16_t color;
    FixedRender(int16_t w, int16_t h, uint16_t c = C_WHITE)
        : naturalWidth(w), naturalHeight(h), color(c) {}
    void performLayout() override {
        size = constraints.constrainSize({naturalWidth, naturalHeight});
    }
    void paintWorker(Canvas* c, Point p) override {
        c->fillRect(p.x, p.y, naturalWidth, naturalHeight, color);
    }
};

int visited = 0;
static void countChild(RenderObject*) {
    ++visited;
}
std::vector<RenderObject*>* collected = nullptr;
static void collectChild(RenderObject* child) {
    collected->push_back(child);
}

void testFlexContract() {
    // Weighted minima must fill the finite budget exactly: the legacy oracle
    // yielded 80 + 50 = 130 here; the bounded contract is 80 + 20 = 100.
    RenderRow* minima = new RenderRow();
    FixedRender* min80 = new FixedRender(0, 4);
    FixedRender* min20 = new FixedRender(0, 4);
    min80->flex = min20->flex = 1;
    min80->preferredMinSize = 80;
    min20->preferredMinSize = 20;
    minima->children.push_back(min80);
    min80->parent = minima;
    minima->children.push_back(min20);
    min20->parent = minima;
    minima->layout({0, 100, 0, 20});
    CHECK(min80->size.width == 80 && min20->size.width == 20);
    CHECK(minima->size.width == 100 && minima->contentMainExtent == 100);
    CHECK(minima->overflowMainExtent == 0);
    delete minima;

    // Remainder redistribution is explicit and lossless: 101 / (1+2+3)
    // becomes 16, 34, 51. The legacy independent truncation was 16,33,50.
    RenderRow* fractional = new RenderRow();
    FixedRender* f1 = new FixedRender(0, 2);
    FixedRender* f2 = new FixedRender(0, 2);
    FixedRender* f3 = new FixedRender(0, 2);
    f1->flex = 1;
    f2->flex = 2;
    f3->flex = 3;
    fractional->children.push_back(f1);
    f1->parent = fractional;
    fractional->children.push_back(f2);
    f2->parent = fractional;
    fractional->children.push_back(f3);
    f3->parent = fractional;
    fractional->layout({0, 101, 0, 10});
    CHECK(f1->size.width == 16 && f2->size.width == 34 && f3->size.width == 51);
    CHECK(fractional->contentMainExtent == 101 && fractional->size.width == 101);
    delete fractional;

    // Unsatisfiable minima remain visible in content extent, while the
    // reported box is constrained and overflow is diagnosed explicitly.
    RenderRow* overflow = new RenderRow();
    FixedRender* o1 = new FixedRender(0, 3);
    FixedRender* o2 = new FixedRender(0, 3);
    o1->flex = o2->flex = 1;
    o1->preferredMinSize = 80;
    o2->preferredMinSize = 30;
    overflow->children.push_back(o1);
    o1->parent = overflow;
    overflow->children.push_back(o2);
    o2->parent = overflow;
    overflow->layout({0, 100, 0, 10});
    CHECK(o1->size.width == 80 && o2->size.width == 30);
    CHECK(overflow->contentMainExtent == 110 && overflow->size.width == 100);
    CHECK(overflow->overflowMainExtent == 10);
    delete overflow;

    // 8001 is finite; only the named sentinel means unbounded. Finite flex
    // consumes the available width, while unbounded flex keeps its minimum.
    RenderRow* finite = new RenderRow();
    FixedRender* finiteChild = new FixedRender(0, 2);
    finiteChild->flex = 1;
    finiteChild->preferredMinSize = 7;
    finite->children.push_back(finiteChild);
    finiteChild->parent = finite;
    finite->layout({0, 8001, 0, 10});
    CHECK(finiteChild->size.width == 8001 && finite->size.width == 8001);
    CHECK(finite->overflowMainExtent == 0);
    delete finite;

    RenderRow* unbounded = new RenderRow();
    FixedRender* unboundedChild = new FixedRender(0, 2);
    unboundedChild->flex = 1;
    unboundedChild->preferredMinSize = 7;
    unbounded->children.push_back(unboundedChild);
    unboundedChild->parent = unbounded;
    unbounded->layout({0, BoxConstraints::Unbounded, 0, 10});
    CHECK(unboundedChild->size.width == 7 && unbounded->size.width == 7);
    delete unbounded;

    // Tight parent bounds, minimums, and zero-size parents all report a size
    // within the parent's contract without hiding content overflow.
    RenderRow* tight = new RenderRow();
    FixedRender* tightChild = new FixedRender(0, 4);
    tightChild->flex = 1;
    tightChild->preferredMinSize = 8;
    tight->children.push_back(tightChild);
    tightChild->parent = tight;
    tight->layout(BoxConstraints::tight(12, 9));
    CHECK(tight->size.width == 12 && tight->size.height == 9);
    CHECK(tightChild->size.width == 12 && tight->overflowMainExtent == 0);
    delete tight;

    RenderRow* zero = new RenderRow();
    FixedRender* zeroChild = new FixedRender(0, 2);
    zeroChild->flex = 1;
    zeroChild->preferredMinSize = 8;
    zero->children.push_back(zeroChild);
    zeroChild->parent = zero;
    zero->layout(BoxConstraints::tight(0, 0));
    CHECK(zero->size.width == 0 && zero->size.height == 0);
    CHECK(zero->contentMainExtent == 8 && zero->overflowMainExtent == 8);
    delete zero;

    // Same finite/unbounded distinction applies to the vertical main axis.
    RenderColumn* vertical = new RenderColumn();
    FixedRender* verticalChild = new FixedRender(2, 0);
    verticalChild->flex = 1;
    verticalChild->preferredMinSize = 5;
    vertical->children.push_back(verticalChild);
    verticalChild->parent = vertical;
    vertical->layout({0, 10, 0, BoxConstraints::Unbounded});
    CHECK(verticalChild->size.height == 5 && vertical->size.height == 5);
    delete vertical;
}

void testVBoxPositionsAndStackOrder() {
    VBox* box = new VBox({new SizedBox(6, 4), new SizedBox(8, 5)});
    RenderVBox* root = static_cast<RenderVBox*>(box->createRenderObject());
    delete box;
    root->layout(BoxConstraints::loose({20, 10}));
    std::vector<RenderObject*> children;
    collected = &children;
    root->visitChildren(collectChild);
    collected = nullptr;
    CHECK(root->size.width == 20 && root->size.height == 9);
    CHECK(children.size() == 2);
    if (children.size() == 2) {
        CHECK(children[0]->pos.x == 7 && children[0]->pos.y == 0);
        CHECK(children[1]->pos.x == 6 && children[1]->pos.y == 4);
    }
    delete root;

    RenderStack* stack = new RenderStack(Alignment::TopLeft);
    FixedRender* bottom = new FixedRender(5, 5, C_WHITE);
    FixedRender* top = new FixedRender(5, 5, C_BLACK);
    stack->children.push_back(bottom);
    bottom->parent = stack;
    stack->children.push_back(top);
    top->parent = stack;
    stack->layout(BoxConstraints::loose({12, 12}));
    children.clear();
    collected = &children;
    stack->visitChildren(collectChild);
    collected = nullptr;
    CHECK(children.size() == 2 && children[0] == bottom && children[1] == top);
    TestCanvas canvas;
    canvas.resetClipRect();
    stack->paint(&canvas, {2, 2});
    CHECK(canvas.at(2, 2) == 0);  // second/top child paints after the first
    delete stack;
}

void testViewportClipAndRestore() {
    TestCanvas canvas;
    canvas.resetClipRect();
    const Rect ancestor = {10, 8, 30, 24};
    canvas.setClipRect(ancestor);

    RenderViewport* outer =
        new RenderViewport(new RenderViewport(new FixedRender(20, 20), ViewportAxis::Vertical),
                           ViewportAxis::Vertical);
    outer->layout(BoxConstraints::tight(8, 7));
    outer->scrollOffset = {0, -1};
    RenderViewport* inner = static_cast<RenderViewport*>(outer->child);
    inner->scrollOffset = {0, -1};
    outer->paint(&canvas, {8, 6});

    // The nested viewports start outside the ancestor's top-left; their
    // intersection cannot widen the caller clip, and both RAII scopes restore.
    CHECK(canvas.at(10, 8) == 1);
    CHECK(canvas.at(15, 12) == 1);
    CHECK(canvas.at(16, 12) == 0);  // outer viewport right edge is exclusive
    Rect restored = canvas.getClipRect();
    CHECK(restored.left == ancestor.left && restored.top == ancestor.top &&
          restored.right == ancestor.right && restored.bottom == ancestor.bottom);
    delete outer;
}

void testListViewAndScrollColumn() {
    TestCanvas canvas;
    canvas.resetClipRect();
    const Rect outerClip = {5, 4, 30, 28};
    canvas.setClipRect(outerClip);

    ListView* list = new ListView({new SizedBox(8, 8), new SizedBox(8, 8), new SizedBox(8, 8)});
    RenderObject* listNode = list->createRenderObject();
    delete list;
    listNode->layout(BoxConstraints::tight(16, 10));
    CHECK(listNode->size.width == 16 && listNode->size.height == 10);
    listNode->paint(&canvas, {8, 7});
    Rect afterList = canvas.getClipRect();
    CHECK(afterList.left == outerClip.left && afterList.top == outerClip.top &&
          afterList.right == outerClip.right && afterList.bottom == outerClip.bottom);
    delete listNode;

    ScrollColumn* column = new ScrollColumn({new SizedBox(8, 8), new SizedBox(8, 8)});
    RenderObject* scrollNode = column->createRenderObject();
    delete column;
    scrollNode->layout(BoxConstraints::tight(16, 10));
    CHECK(scrollNode->size.width == 16 && scrollNode->size.height == 10);
    scrollNode->paint(&canvas, {8, 7});
    Rect afterScroll = canvas.getClipRect();
    CHECK(afterScroll.left == outerClip.left && afterScroll.top == outerClip.top &&
          afterScroll.right == outerClip.right && afterScroll.bottom == outerClip.bottom);
    delete scrollNode;
}

int presses = 0;
void pressed() {
    ++presses;
}
class TapOnly final : public RenderObject {
   public:
    bool canFocus() override { return true; }
    void onTap() override { ++presses; }
    void performLayout() override { size = constraints.constrainSize({8, 8}); }
    void paintWorker(Canvas*, Point) override {}
};

void testListEventCompatibility() {
    RenderListView list;
    list.hasFocus = true;
    RenderButton* button = new RenderButton(pressed);
    button->child = new FixedRender(8, 8);
    button->child->parent = button;
    TapOnly* legacyItem = new TapOnly;
    list.children.push_back(button);
    button->parent = &list;
    list.children.push_back(legacyItem);
    legacyItem->parent = &list;
    list.layout(BoxConstraints::tight(16, 10));
    presses = 0;
    CHECK(list.handleEvent({EVT_OK, 0}) && presses == 1);
    CHECK(list.handleEvent({EVT_DOWN, 0}) && list.selectedIndex == 1);
    list.layout(BoxConstraints::tight(16, 10));
    CHECK(list.handleEvent({EVT_OK, 0}) && presses == 2);
    CHECK(list.targetScrollY == 6);
    CHECK(button->hasFocus == false && legacyItem->hasFocus);
}

void testClipAndCoordinateEdges() {
    CHECK(!(Rect{0, 0, 10, 10}.intersects({4, 4, 4, 8})));
    TestCanvas canvas;
    canvas.resetClipRect();
    canvas.fillRect(32760, 32760, 200, 200, C_WHITE);
    canvas.drawFastHLine(32760, 0, 200, C_WHITE);
    canvas.drawFastVLine(0, 32760, 200, C_WHITE);
    canvas.fillRect(-32760, -32760, 32767, 32767, C_WHITE);
    CHECK(canvas.at(0, 0) == 1 && canvas.at(6, 6) == 1 && canvas.at(7, 7) == 0);
    canvas.clear();
    RenderClipRect clipped(new FixedRender(20, 20));
    clipped.layout(BoxConstraints::tight(7, 6));
    const Rect ancestor{4, 3, 12, 10};
    canvas.setClipRect(ancestor);
    clipped.paint(&canvas, {2, 2});
    CHECK(canvas.at(4, 3) == 1 && canvas.at(8, 7) == 1 && canvas.at(9, 7) == 0);
    const Rect restored = canvas.getClipRect();
    CHECK(std::memcmp(&ancestor, &restored, sizeof(Rect)) == 0);
}

void testUnboundedComposition() {
    const BoxConstraints infinite = {0, BoxConstraints::Unbounded, 0, BoxConstraints::Unbounded};
    const BoxConstraints deflated = infinite.deflate(Insets::all(2));
    CHECK(!deflated.hasBoundedWidth() && !deflated.hasBoundedHeight());
    Widget* widget = new Viewport(new Padding(
        new Column({new SizedBox(8, 8), new Spacer(), new SizedBox(8, 8)}), Insets::all(2)));
    RenderViewport* viewport = static_cast<RenderViewport*>(widget->createRenderObject());
    delete widget;
    viewport->layout(BoxConstraints::tight(20, 20));
    CHECK(viewport->child->size.height == 20);
    RenderPadding* padding = static_cast<RenderPadding*>(viewport->child);
    RenderColumn* column = static_cast<RenderColumn*>(padding->child);
    CHECK(!column->constraints.hasBoundedHeight());
    CHECK(column->children[1]->size.height == 0 && column->children[2]->pos.y == 8);
    delete viewport;

    // Requires a second active-set pass: 10:1:1, minima 0/60/5,
    // budget 80. Pin 60, then 5, leaving 15 for the first child.
    RenderRow row;
    for (int i = 0; i < 3; ++i) {
        FixedRender* child = new FixedRender(0, 2);
        child->flex = i == 0 ? 10 : 1;
        child->preferredMinSize = i == 0 ? 0 : i == 1 ? 60 : 5;
        child->parent = &row;
        row.children.push_back(child);
    }
    row.layout(BoxConstraints::loose({80, 10}));
    CHECK(row.children[0]->size.width == 15 && row.children[1]->size.width == 60 &&
          row.children[2]->size.width == 5);
    CHECK(row.contentMainExtent == 80 && row.overflowMainExtent == 0);
}
}  // namespace

int main() {
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    testFlexContract();
    testVBoxPositionsAndStackOrder();
    testViewportClipAndRestore();
    testListViewAndScrollColumn();
    testListEventCompatibility();
    testClipAndCoordinateEdges();
    testUnboundedComposition();
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    CHECK(heap.allocations == heap.releases);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("layout core contract failures=%d\n", failures);
    return failures ? 1 : 0;
}
