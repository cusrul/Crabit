// Host contract tests for the original Widget/RenderObject layout protocol.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>

class HeapAdapter final : public NanoUI::MemoryAdapter {
    struct alignas(std::max_align_t) Header {
        size_t bytes;
    };

   public:
    size_t allocations = 0;
    size_t releases = 0;
    size_t liveBlocks = 0;
    size_t liveBytes = 0;
    void* alloc(size_t size) override {
        Header* allocation = static_cast<Header*>(std::malloc(sizeof(Header) + size));
        if (!allocation)
            return nullptr;
        allocation->bytes = size;
        ++allocations;
        ++liveBlocks;
        liveBytes += size;
        return allocation + 1;
    }
    void free(void* ptr) override {
        if (ptr) {
            Header* allocation = static_cast<Header*>(ptr) - 1;
            ++releases;
            --liveBlocks;
            liveBytes -= allocation->bytes;
            std::free(allocation);
        }
    }
};

class PixelCanvas final : public Canvas {
   public:
    unsigned char pixels[96 * 64]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 96; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= width() || y >= height())
            return;
        unsigned char& pixel = pixels[y * width() + x];
        if (color == C_INVERT)
            pixel ^= 1;
        else
            pixel = color == C_WHITE;
    }
};

namespace {
int failures = 0;
int callbackCount = 0;
#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

struct Probe {
    BoxConstraints received = {0, 0, 0, 0};
    Point painted = {0, 0};
    int layouts = 0;
};

class ProbeRender final : public RenderObject {
    Probe& probe;

   public:
    explicit ProbeRender(Probe& p) : probe(p) {}
    void performLayout() override {
        probe.received = constraints;
        ++probe.layouts;
        size = constraints.constrainSize({7, 5});
    }
    void paintWorker(Canvas*, Point absolutePos) override { probe.painted = absolutePos; }
};

class ProbeWidget final : public Widget {
    Probe& probe;

   public:
    explicit ProbeWidget(Probe& p) : probe(p) {}
    RenderObject* createRenderObject() override { return new ProbeRender(probe); }
};

void writeBitmap(const PixelCanvas& canvas) {
    const int scale = 4, width = 96 * scale, height = 64 * scale;
    const unsigned bytes = 54 + width * height * 3;
    unsigned char header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    for (int i = 0; i < 4; ++i)
        header[2 + i] = (bytes >> (8 * i)) & 255;
    header[10] = 54;
    header[14] = 40;
    header[18] = width & 255;
    header[19] = width >> 8;
    header[22] = height & 255;
    header[23] = height >> 8;
    header[26] = 1;
    header[28] = 24;
    const char* path =
        NANOUI_DYNAMIC_STORAGE ? "layout-primitives-dynamic.bmp" : "layout-primitives-pool.bmp";
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    if (!file)
        return;
    CHECK(std::fwrite(header, 1, sizeof header, file) == sizeof header);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            unsigned char c = canvas.pixels[(y / scale) * 96 + x / scale] ? 255 : 0;
            unsigned char rgb[3] = {c, c, c};
            CHECK(std::fwrite(rgb, 1, 3, file) == 3);
        }
    }
    CHECK(std::fclose(file) == 0);
}

RenderObject* createTree(Widget* widget) {
    RenderObject* root = widget->createRenderObject();
    CHECK(root != nullptr);
    delete widget;  // RenderObjects own their independent render subtree.
    return root;
}

void destroyTree(RenderObject* root) {
    delete root;
}

void checkClean(const HeapAdapter& heap) {
    NanoUI::MemoryPool& pool = NanoUI::MemoryPool::instance();
    CHECK(pool.getWidgetUsage() == 0);
    CHECK(pool.getRenderUsage() == 0);
    CHECK(heap.liveBlocks == 0);
    CHECK(heap.liveBytes == 0);
    CHECK(heap.allocations == heap.releases);
}

void checkPrimitiveGeometry() {
    Probe probe;
    RenderObject* root = createTree(new Padding(new ProbeWidget(probe), Insets{2, 3, 4, 5}));
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->size.width == 13 && root->size.height == 13);
    root->paint(nullptr, {11, 13});
    CHECK(probe.painted.x == 13 && probe.painted.y == 16);
    CHECK(probe.received.maxWidth == 34 && probe.received.maxHeight == 22);
    CHECK(probe.received.minWidth == 0 && probe.received.minHeight == 0);
    destroyTree(root);

    Probe uniform;
    root = createTree(new Padding(new ProbeWidget(uniform), Insets::all(2)));
    root->layout(BoxConstraints::loose({30, 20}));
    CHECK(root->size.width == 11 && root->size.height == 9);
    destroyTree(root);

    root = createTree(new Padding(nullptr, Insets{3, 2, 5, 4}));
    root->layout(BoxConstraints::loose({32, 24}));
    CHECK(root->size.width == 8 && root->size.height == 6);
    destroyTree(root);

    Probe oversized;
    root = createTree(new Padding(new ProbeWidget(oversized), Insets{20, 18, 21, 19}));
    root->layout(BoxConstraints::loose({12, 10}));
    CHECK(root->size.width >= 0 && root->size.width <= 12);
    CHECK(root->size.height >= 0 && root->size.height <= 10);
    CHECK(oversized.received.minWidth <= oversized.received.maxWidth);
    CHECK(oversized.received.minHeight <= oversized.received.maxHeight);
    destroyTree(root);
}

void checkConstraintsAndAlign() {
    const BoxConstraints loose = BoxConstraints::loose({30, 20});
    const Size constrained = loose.constrainSize({40, 8});
    CHECK(constrained.width == 30 && constrained.height == 8);
    const BoxConstraints loosened = BoxConstraints::tight(12, 9).loosen();
    CHECK(loosened.minWidth == 0 && loosened.maxWidth == 12);
    CHECK(loosened.minHeight == 0 && loosened.maxHeight == 9);
    const BoxConstraints deflated = BoxConstraints{2, 30, 3, 20}.deflate(Insets{1, 2, 4, 5});
    CHECK(deflated.minWidth == 0 && deflated.maxWidth == 25);
    CHECK(deflated.minHeight == 0 && deflated.maxHeight == 13);
    const BoxConstraints enforced =
        BoxConstraints{0, 40, 0, 30}.enforce(BoxConstraints{5, 20, 4, 18});
    CHECK(enforced.minWidth == 5 && enforced.maxWidth == 20);
    CHECK(enforced.minHeight == 4 && enforced.maxHeight == 18);

    Probe probe;
    RenderObject* root =
        createTree(new ConstrainedBox(new ProbeWidget(probe), BoxConstraints::tight(20, 14)));
    root->layout(BoxConstraints::loose({50, 40}));
    CHECK(root->size.width == 20 && root->size.height == 14);
    destroyTree(root);

    Probe tight;
    root = createTree(new ConstrainedBox(new ProbeWidget(tight), BoxConstraints::tight(20, 14)));
    root->layout(BoxConstraints::tight(9, 8));
    CHECK(root->size.width == 9 && root->size.height == 8);
    destroyTree(root);

    Probe centered;
    root = createTree(new Align(new ProbeWidget(centered), Alignment::Center));
    root->layout(BoxConstraints::loose({30, 20}));
    CHECK(root->size.width == 7 && root->size.height == 5);
    root->paint(nullptr, {5, 6});
    CHECK(centered.painted.x == 5 && centered.painted.y == 6);
    destroyTree(root);

    Probe topLeft;
    root = createTree(new Align(new ProbeWidget(topLeft), Alignment::TopLeft));
    root->layout(BoxConstraints::tight(30, 20));
    root->paint(nullptr, {2, 3});
    CHECK(topLeft.painted.x == 2 && topLeft.painted.y == 3);
    destroyTree(root);

    Probe expanded;
    root = createTree(new Align(new ProbeWidget(expanded), Alignment::BottomRight, true, true));
    root->layout(BoxConstraints::loose({30, 20}));
    CHECK(root->size.width == 30 && root->size.height == 20);
    root->paint(nullptr, {4, 7});
    CHECK(expanded.painted.x == 27 && expanded.painted.y == 22);
    destroyTree(root);

    Probe bottomRight;
    root = createTree(new Align(new ProbeWidget(bottomRight), Alignment::BottomRight));
    root->layout(BoxConstraints::tight(30, 20));
    root->paint(nullptr, {3, 4});
    CHECK(bottomRight.painted.x == 26 && bottomRight.painted.y == 19);
    destroyTree(root);
}

void checkContainerAndRepeatedLayout() {
    RenderObject* root = createTree(new Container(new SizedBox(8, 6), 24, 18));
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->size.width == 24 && root->size.height == 18);
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->size.width == 24 && root->size.height == 18);
    destroyTree(root);

    root = createTree(new Container(new SizedBox(9, 7), 0, 0, 2));
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->size.width == 13 && root->size.height == 11);
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->size.width == 13 && root->size.height == 11);
    destroyTree(root);

    root = createTree(new Container(new SizedBox(8, 6), 20, 0));
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->size.width == 20 && root->size.height == 6);
    root->layout(BoxConstraints::tight(32, 22));
    CHECK(root->size.width == 32 && root->size.height == 22);
    destroyTree(root);

    root = createTree(new Container(new SizedBox(8, 6)));
    root->layout(BoxConstraints::tight(28, 16));
    CHECK(root->size.width == 28 && root->size.height == 16);
    destroyTree(root);
}

void checkLegacyContentReplacement() {
    Probe first;
    RenderContainer* root = new RenderContainer(24, 18, 2, true, 3);
    root->child = new ProbeRender(first);
    root->child->parent = root;
    root->layout(BoxConstraints::loose({40, 30}));
    // Exercise geometry with a non-painting leaf, without requiring a Canvas.
    Point local{0, 0};
    for (RenderObject* n = root->child; n != root; n = n->parent) {
        CHECK(n->parent != nullptr);
        local.x += n->pos.x;
        local.y += n->pos.y;
    }
    CHECK(local.x == 8 && local.y == 6);
    CHECK(root->child->size.width == 7 && root->child->size.height == 5);
    CHECK(first.layouts == 1);
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(first.layouts == 1);  // unchanged constraints use cached layout
    root->child->markNeedsLayout();
    CHECK(root->needsLayout);
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(first.layouts == 2);

    Probe replacement;
    delete root->child;
    root->child = new ProbeRender(replacement);
    root->child->parent = root;
    root->markNeedsLayout();
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->child->size.width == 7 && replacement.layouts == 1);
    root->padding = 4;
    root->markNeedsLayout();
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(replacement.received.maxWidth == 16 && replacement.received.maxHeight == 10);
    CHECK(replacement.layouts == 2);
    delete root;

    Widget* widget = new Container(new SizedBox(8, 6), 24, 18, 2, true, 3);
    root = static_cast<RenderContainer*>(widget->createRenderObject());
    delete widget;
    root->layout(BoxConstraints::loose({40, 30}));
    delete root->child;
    root->child = new RenderSizedBox(10, 8);
    root->markNeedsLayout();
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->child->size.width == 10 && root->child->size.height == 8);
    delete root;
}

class TeardownRender final : public RenderObject {
    bool& destroyed;

   public:
    explicit TeardownRender(bool& flag) : destroyed(flag) {}
    ~TeardownRender() override {
        CHECK(parent == nullptr);
        // Before the fix, fail without dereferencing a freed parent. Once
        // detached, exercise the destructor notification itself as well.
        if (!parent)
            markNeedsLayout();
        destroyed = true;
    }
    void performLayout() override { size = constraints.constrainSize({8, 6}); }
    void paintWorker(Canvas*, Point) override {}
};

void checkDetachedContentTeardown() {
    bool destroyed = false;
    RenderContainer* root = new RenderContainer(24, 18, 2, true, 3);
    root->child = new TeardownRender(destroyed);
    root->child->parent = root;
    root->layout(BoxConstraints::loose({40, 30}));
    delete root;
    CHECK(destroyed);

    destroyed = false;
    root = new RenderContainer(24, 18, 2, true, 3);
    root->child = new TeardownRender(destroyed);
    root->child->parent = root;
    delete root;  // unlaid-out content must also be detached before destruction
    CHECK(destroyed);
}

void onPressed() {
    ++callbackCount;
}
bool foundButton = false;
RenderObject* discoveredButton = nullptr;
void findButton(RenderObject* node) {
    if (!node)
        return;
    if (node->canFocus()) {
        foundButton = true;
        discoveredButton = node;
    }
    node->visitChildren(findButton);
}

void checkDecoratedButtonAndPixels(PixelCanvas& canvas) {
    callbackCount = 0;
    RenderObject* root = createTree(new DecoratedBox(
        new Button(new Container(new SizedBox(14, 10), 20, 16, 1, true, 3), onPressed), true, 4));
    root->layout(BoxConstraints::loose({40, 30}));
    CHECK(root->getRadius() == 4);
    root->paint(&canvas, {8, 7});
    CHECK(canvas.pixels[7 * 96 + 8] == 0);
    CHECK(canvas.pixels[7 * 96 + 12] == 1);
    CHECK(canvas.pixels[7 * 96 + 23] == 1);  // straight segment before the right arc
    CHECK(canvas.pixels[7 * 96 + 27] == 0);  // rounded top-right corner

    foundButton = false;
    discoveredButton = nullptr;
    root->visitChildren(findButton);
    CHECK(!root->canFocus());
    CHECK(foundButton);
    CHECK(discoveredButton != nullptr && discoveredButton->getRadius() == 3);
    CHECK(!root->handleEvent({EVT_OK, 0}) && callbackCount == 0);

    root->visitChildren([](RenderObject* node) {
        if (node->canFocus())
            node->hasFocus = true;
    });
    // Dispatch by walking the tree, as WindowManager does for discovery.
    root->visitChildren([](RenderObject* node) {
        if (node->canFocus())
            CHECK(node->handleEvent({EVT_OK, 0}));
    });
    CHECK(callbackCount == 1);
    writeBitmap(canvas);
    destroyTree(root);
}

void checkFocusThroughComposition() {
    callbackCount = 0;
    RenderObject* root = createTree(
        new Container(new Padding(new Button(new SizedBox(12, 8), onPressed), Insets::all(2)), 40,
                      24, 1, true, 4));
    root->layout(BoxConstraints::loose({64, 40}));
    discoveredButton = nullptr;
    root->visitChildren(findButton);
    CHECK(!root->canFocus() && discoveredButton != nullptr);
    if (discoveredButton) {
        Point position{0, 0};
        RenderObject* node = discoveredButton;
        while (node && node != root) {
            CHECK(node->parent != nullptr);
            position.x += node->pos.x;
            position.y += node->pos.y;
            node = node->parent;
        }
        CHECK(node == root && position.x == 14 && position.y == 8);
        discoveredButton->hasFocus = true;
        CHECK(discoveredButton->handleEvent({EVT_OK, 0}) && callbackCount == 1);
    }
    destroyTree(root);
}
}  // namespace

int main() {
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    PixelCanvas canvas;
    canvas.clear();
    canvas.resetClipRect();

    checkPrimitiveGeometry();
    checkClean(heap);
    checkConstraintsAndAlign();
    checkClean(heap);
    checkContainerAndRepeatedLayout();
    checkClean(heap);
    checkLegacyContentReplacement();
    checkClean(heap);
    checkDetachedContentTeardown();
    checkClean(heap);
    checkDecoratedButtonAndPixels(canvas);
    checkClean(heap);
    checkFocusThroughComposition();
    checkClean(heap);

    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("layout primitives failures=%d allocations=%zu releases=%zu\n", failures,
                heap.allocations, heap.releases);
    return failures ? 1 : 0;
}
