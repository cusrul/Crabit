// Behavioral oracle for the original Row and Column algorithms at 7e3d8af.
// Keep the two reference layout routines separate: they are intentionally not
// factored into a shared axis-generic implementation.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
int failures = 0;
int casesRun = 0;
enum { SheetTiles = 13 };
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            ++failures;                                 \
            std::printf("line %d: %s\n", __LINE__, #x); \
        }                                               \
    } while (false)

class MallocAdapter final : public NanoUI::MemoryAdapter {
   public:
    int live = 0;
    void* alloc(size_t n) override {
        void* p = std::malloc(n);
        if (p)
            ++live;
        return p;
    }
    void free(void* p) override {
        if (p) {
            --live;
            std::free(p);
        }
    }
};

class PixelCanvas final : public Canvas {
   public:
    enum { W = 128, H = 64 };
    unsigned char pixels[W * H]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return W; }
    int16_t height() override { return H; }
    void drawPixel(int16_t x, int16_t y, uint16_t c) override {
        if (x >= 0 && y >= 0 && x < W && y < H)
            pixels[y * W + x] = c == C_INVERT ? !pixels[y * W + x] : c == C_WHITE;
    }
};

class MarkRender final : public RenderObject {
    int16_t w_, h_;

   public:
    int layouts = 0;
    MarkRender(int16_t w, int16_t h) : w_(w), h_(h) {}
    void performLayout() override {
        ++layouts;
        size = constraints.constrainSize({w_, h_});
    }
    void paintWorker(Canvas* c, Point p) override {
        c->fillRect(p.x, p.y, size.width, size.height, C_WHITE);
    }
    bool canFocus() override { return true; }
};
class MarkWidget final : public Widget {
    int16_t w_, h_;

   public:
    MarkWidget(int16_t w, int16_t h, int flex = 0, int16_t min = 0)
        : w_(w), h_(h), flex_(flex), min_(min) {}
    RenderObject* createRenderObject() override {
        MarkRender* r = new MarkRender(w_, h_);
        r->flex = flex_;
        r->preferredMinSize = min_;
        return r;
    }

   private:
    int flex_;
    int16_t min_;
};

// Extracted from git show 7e3d8af:libraries/NanoUI/src/NanoUI/product/widgets/flex.h.
class ReferenceRow final : public RenderObject {
   public:
    std::vector<RenderObject*> children;
    explicit ReferenceRow(const std::vector<RenderObject*>& c) : children(c) {
        for (auto p : children)
            p->parent = this;
    }
    ~ReferenceRow() override {
        for (auto p : children)
            delete p;
    }
    void visitChildren(RenderObjectVisitor v) override {
        for (auto p : children)
            v(p);
    }
    void performLayout() override {
        int16_t totalFlex = 0, rigidWidth = 0, maxHeight = 0, minFlexWidth = 0;
        for (auto child : children) {
            if (child->flex == 0) {
                child->layout(BoxConstraints::loose(constraints.maxSize()));
                rigidWidth += child->size.width;
                if (child->size.height > maxHeight)
                    maxHeight = child->size.height;
            } else {
                totalFlex += child->flex;
                minFlexWidth += child->preferredMinSize;
            }
        }
        int16_t available = constraints.maxWidth - rigidWidth;
        if (available < minFlexWidth)
            available = minFlexWidth;
        bool isUnbounded = constraints.maxWidth > 8000;
        int16_t xPos = 0;
        for (auto child : children) {
            if (child->flex > 0) {
                int16_t flexW = child->preferredMinSize;
                if (!isUnbounded && totalFlex > 0) {
                    int16_t distributed =
                        static_cast<int16_t>((float)available * child->flex / totalFlex);
                    if (distributed > flexW)
                        flexW = distributed;
                }
                child->layout({flexW, flexW, 0, constraints.maxHeight});
            }
            child->pos = {xPos, 0};
            if (child->size.height > maxHeight)
                maxHeight = child->size.height;
            xPos += child->size.width;
        }
        for (auto child : children)
            child->pos.y = static_cast<int16_t>((maxHeight - child->size.height) / 2);
        size = {xPos, maxHeight};
    }
    void paintWorker(Canvas* c, Point p) override {
        for (auto child : children)
            child->paint(c, p);
    }
};

class ReferenceColumn final : public RenderObject {
   public:
    std::vector<RenderObject*> children;
    explicit ReferenceColumn(const std::vector<RenderObject*>& c) : children(c) {
        for (auto p : children)
            p->parent = this;
    }
    ~ReferenceColumn() override {
        for (auto p : children)
            delete p;
    }
    void visitChildren(RenderObjectVisitor v) override {
        for (auto p : children)
            v(p);
    }
    void performLayout() override {
        int16_t totalFlex = 0, rigidHeight = 0, maxWidth = 0, minFlexHeight = 0;
        for (auto child : children) {
            if (child->flex == 0) {
                child->layout(BoxConstraints::loose(constraints.maxSize()));
                rigidHeight += child->size.height;
                if (child->size.width > maxWidth)
                    maxWidth = child->size.width;
            } else {
                totalFlex += child->flex;
                minFlexHeight += child->preferredMinSize;
            }
        }
        int16_t available = constraints.maxHeight - rigidHeight;
        if (available < minFlexHeight)
            available = minFlexHeight;
        bool isUnbounded = constraints.maxHeight > 8000;
        int16_t yPos = 0;
        for (auto child : children) {
            if (child->flex > 0) {
                int16_t flexH = child->preferredMinSize;
                if (!isUnbounded && totalFlex > 0) {
                    int16_t distributed =
                        static_cast<int16_t>((float)available * child->flex / totalFlex);
                    if (distributed > flexH)
                        flexH = distributed;
                }
                child->layout({0, constraints.maxWidth, flexH, flexH});
            }
            child->pos = {0, yPos};
            if (child->size.width > maxWidth)
                maxWidth = child->size.width;
            yPos += child->size.height;
        }
        size = {maxWidth, yPos};
    }
    void paintWorker(Canvas* c, Point p) override {
        for (auto child : children)
            child->paint(c, p);
    }
};

int visited = 0;
static void visitCount(RenderObject* r) {
    (void)r;
    ++visited;
}
std::vector<RenderObject*>* collecting = nullptr;
static void collectChild(RenderObject* c) {
    collecting->push_back(c);
}
static RenderObject* marked(int16_t w, int16_t h, int flex = 0, int16_t min = 0) {
    RenderObject* r = new MarkRender(w, h);
    r->flex = flex;
    r->preferredMinSize = min;
    return r;
}
static std::vector<RenderObject*> refs(const std::vector<std::vector<int> >& specs) {
    std::vector<RenderObject*> out;
    for (const auto& s : specs)
        out.push_back(marked(s[0], s[1], s.size() > 2 ? s[2] : 0, s.size() > 3 ? s[3] : 0));
    return out;
}
static std::vector<Widget*> widgets(const std::vector<std::vector<int> >& specs) {
    std::vector<Widget*> out;
    for (const auto& s : specs)
        out.push_back(new MarkWidget(s[0], s[1], s.size() > 2 ? s[2] : 0, s.size() > 3 ? s[3] : 0));
    return out;
}
static bool equalTree(RenderObject* a, RenderObject* b) {
    if (a->size.width != b->size.width || a->size.height != b->size.height ||
        a->pos.x != b->pos.x || a->pos.y != b->pos.y)
        return false;
    if (a->constraints.minWidth != b->constraints.minWidth ||
        a->constraints.maxWidth != b->constraints.maxWidth ||
        a->constraints.minHeight != b->constraints.minHeight ||
        a->constraints.maxHeight != b->constraints.maxHeight)
        return false;
    const MarkRender* am = dynamic_cast<const MarkRender*>(a);
    const MarkRender* bm = dynamic_cast<const MarkRender*>(b);
    if (am && bm && am->layouts != bm->layouts)
        return false;
    std::vector<RenderObject*> ac, bc;
    collecting = &ac;
    a->visitChildren(collectChild);
    collecting = &bc;
    b->visitChildren(collectChild);
    collecting = nullptr;
    if (ac.size() != bc.size())
        return false;
    for (size_t i = 0; i < ac.size(); ++i)
        if (ac[i]->parent != a || bc[i]->parent != b || !equalTree(ac[i], bc[i]))
            return false;
    return true;
}

static void writeBmp(const unsigned char* pixels, const char* path) {
    const int scale = 4, w = PixelCanvas::W * scale, h = PixelCanvas::H * scale * SheetTiles;
    const unsigned bytes = 54 + w * h * 3;
    unsigned char header[54]{};
    header[0] = 'B';
    header[1] = 'M';
    header[10] = 54;
    header[14] = 40;
    header[26] = 1;
    header[28] = 24;
    for (int i = 0; i < 4; ++i) {
        header[2 + i] = (bytes >> (8 * i)) & 255;
        header[18 + i] = (w >> (8 * i)) & 255;
        header[22 + i] = (h >> (8 * i)) & 255;
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::perror(path);
        ++failures;
        return;
    }
    bool ok = std::fwrite(header, 1, 54, f) == 54;
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            unsigned char v = pixels[(y / scale) * PixelCanvas::W + x / scale] ? 255 : 0;
            unsigned char rgb[3] = {v, v, v};
            ok = (std::fwrite(rgb, 1, 3, f) == 3) && ok;
        }
    ok = (std::fclose(f) == 0) && ok;
    CHECK(ok);
}

// Reference and actual are separately constructed trees; every assertion is
// against the same constraints and nonzero root origin.
static void compare(const char* label, bool row, const std::vector<std::vector<int> >& spec,
                    BoxConstraints k, bool paint, unsigned char* oldSheet, unsigned char* newSheet,
                    int tile) {
    ++casesRun;
    RenderObject* oracle = row ? static_cast<RenderObject*>(new ReferenceRow(refs(spec)))
                               : static_cast<RenderObject*>(new ReferenceColumn(refs(spec)));
    std::vector<Widget*> ws = widgets(spec);
    Widget* widget = row ? static_cast<Widget*>(new Row(ws)) : static_cast<Widget*>(new Column(ws));
    RenderObject* actual = widget->createRenderObject();
    delete widget;
    oracle->layout(k);
    actual->layout(k);
    CHECK(equalTree(oracle, actual));
    visited = 0;
    actual->visitChildren(visitCount);
    CHECK(visited == static_cast<int>(spec.size()));
    PixelCanvas oldCanvas, newCanvas;
    oldCanvas.clear();
    newCanvas.clear();
    oldCanvas.resetClipRect();
    newCanvas.resetClipRect();
    oldCanvas.drawText(1, 1, label, 8);
    newCanvas.drawText(1, 1, label, 8);
    if (paint) {
        oracle->paint(&oldCanvas, {5, 20});
        actual->paint(&newCanvas, {5, 20});
    }
    CHECK(std::memcmp(oldCanvas.pixels, newCanvas.pixels, sizeof oldCanvas.pixels) == 0);
    std::memcpy(oldSheet + tile * PixelCanvas::W * PixelCanvas::H, oldCanvas.pixels,
                sizeof oldCanvas.pixels);
    std::memcpy(newSheet + tile * PixelCanvas::W * PixelCanvas::H, newCanvas.pixels,
                sizeof newCanvas.pixels);
    // Re-layout after both changed constraints and explicit dirty propagation.
    BoxConstraints next = BoxConstraints::loose({72, 40});
    oracle->markNeedsLayout();
    actual->markNeedsLayout();
    oracle->layout(next);
    actual->layout(next);
    if (!row && !std::strcmp(label, "column-flex")) {
        // The first 60px layout has no remainder/minimum conflict, but the
        // second 40px layout pins the first minimum and redistributes 32px.
        CHECK(oracle->size.height == 41 && actual->size.height == 40);
        std::vector<RenderObject*> kids;
        collecting = &kids;
        actual->visitChildren(collectChild);
        collecting = nullptr;
        CHECK(kids.size() == 3);
        if (kids.size() == 3)
            CHECK(kids[0]->size.height == 8 && kids[1]->size.height == 12 &&
                  kids[2]->size.height == 20);
    } else
        CHECK(equalTree(oracle, actual));
    delete actual;
    delete oracle;
}

void run() {
    unsigned char before[PixelCanvas::W * PixelCanvas::H * SheetTiles]{},
        after[PixelCanvas::W * PixelCanvas::H * SheetTiles]{};
    const BoxConstraints loose = BoxConstraints::loose({100, 60});
    compare("row-center", true, {{12, 8}, {8, 16}, {10, 10}}, loose, true, before, after, 0);
    compare("column-left", false, {{12, 8}, {8, 16}, {10, 10}}, loose, true, before, after, 1);
    compare("empty-row", true, {}, loose, false, before, after, 2);
    compare("empty-col", false, {}, loose, false, before, after, 3);
    compare("loose", false, {{7, 5}, {9, 6}}, BoxConstraints::loose({28, 20}), true, before, after,
            5);
    // These cases intentionally differ from the old oracle and are asserted
    // below as the new bounded-layout contract, not equality comparisons.
    compare("column-flex", false, {{4, 0, 1, 8}, {6, 0, 2, 0}, {8, 0, 3, 0}},
            BoxConstraints{0, 20, 0, 60}, true, before, after, 10);
    compare("column-unbounded-sentinel", false, {{4, 0, 1, 7}},
            BoxConstraints{0, 20, 0, BoxConstraints::Unbounded}, true, before, after, 11);
    // Tight parent size used to be violated by the legacy Column routine.
    // Remainder is redistributed rather than lost to independent truncation:
    // legacy reference 16/33/50 (99), bounded algorithm 16/34/51 (101).
    {
        RenderObject* old = new ReferenceRow(refs({{0, 4, 1}, {0, 4, 2}, {0, 4, 3}}));
        old->layout({0, 101, 0, 30});
        std::vector<RenderObject*> kids;
        collecting = &kids;
        old->visitChildren(collectChild);
        collecting = nullptr;
        CHECK(kids.size() == 3 && old->size.width == 99);
        if (kids.size() == 3)
            CHECK(kids[0]->size.width == 16 && kids[1]->size.width == 33 &&
                  kids[2]->size.width == 50);
        delete old;
    }
    {
        Widget* w =
            new Row({new MarkWidget(0, 4, 1), new MarkWidget(0, 4, 2), new MarkWidget(0, 4, 3)});
        RenderObject* ro = w->createRenderObject();
        delete w;
        ro->layout({0, 101, 0, 30});
        std::vector<RenderObject*> kids;
        collecting = &kids;
        ro->visitChildren(collectChild);
        collecting = nullptr;
        CHECK(kids.size() == 3);
        if (kids.size() == 3)
            CHECK(kids[0]->size.width == 16 && kids[1]->size.width == 34 &&
                  kids[2]->size.width == 51 && ro->size.width == 101);
        delete ro;
    }
    // Deliberate root-size delta: legacy Row reports child content (11x8),
    // integrated bounded Row reports the parent's tight 40x24 size.
    {
        RenderObject* old = new ReferenceRow(refs({{5, 7}, {6, 8}}));
        old->layout(BoxConstraints::tight(40, 24));
        CHECK(old->size.width == 11 && old->size.height == 8);
        delete old;
    }
    {
        Widget* w = new Row({new MarkWidget(5, 7), new MarkWidget(6, 8)});
        RenderObject* ro = w->createRenderObject();
        delete w;
        ro->layout(BoxConstraints::tight(40, 24));
        CHECK(ro->size.width == 40 && ro->size.height == 24);
        delete ro;
    }
    // Public constructor overloads are exercised with both initializer_list and vector.
    {
        Row r({new MarkWidget(3, 4), new MarkWidget(5, 6)});
        RenderObject* ro = r.createRenderObject();
        ro->layout(loose);
        CHECK(ro->size.width == 8);
        delete ro;
    }
    {
        std::vector<Widget*> v{new MarkWidget(3, 4), new MarkWidget(5, 6)};
        Column c(v);
        RenderObject* ro = c.createRenderObject();
        ro->layout(loose);
        CHECK(ro->size.height == 10);
        delete ro;
    }
    // Focus traversal contract without constructing WindowManager.
    {
        RenderObject* r = new ReferenceRow(refs({{4, 4}, {5, 5}, {6, 6}}));
        visited = 0;
        r->visitChildren(visitCount);
        CHECK(visited == 3);
        delete r;
    }
    // A small real Container -> Row -> Column tree checks composite traversal/layout.
    {
        Widget* nested =
            new Container(new Row({new MarkWidget(6, 5),
                                   new Column({new MarkWidget(3, 2), new MarkWidget(4, 3)})}),
                          24, 20, 1, false);
        RenderObject* ro = nested->createRenderObject();
        delete nested;
        ro->layout(loose);
        CHECK(ro->size.width == 24 && ro->size.height == 20);
        delete ro;
    }
    // Real public elastic Widgets, not only synthetic flex metadata.
    {
        Widget* widget = new Row({new SizedBox(10, 8), new Spacer(2, 4),
                                  new Expanded(new SizedBox(0, 6)), new Fill(new SizedBox(0, 4))});
        RenderObject* root = widget->createRenderObject();
        delete widget;
        root->layout(BoxConstraints::loose({100, 20}));
        std::vector<RenderObject*> children;
        collecting = &children;
        root->visitChildren(collectChild);
        collecting = nullptr;
        CHECK(children.size() == 4);
        if (children.size() == 4) {
            CHECK(children[0]->size.width == 10 && children[1]->size.width == 45);
            CHECK(children[2]->size.width == 22 && children[3]->size.width == 23);
        }
        delete root;
    }
    // Deliberate behavior change: old oracle distributes 80+50=130 in 100;
    // the new active-set allocation respects both minima and fits 80+20=100.
    RenderObject* oldFlex = new ReferenceRow(refs({{0, 4, 1, 80}, {0, 4, 1, 0}}));
    oldFlex->layout({0, 100, 0, 20});
    CHECK(oldFlex->size.width == 130);
    delete oldFlex;
    {
        Widget* w = new Row({new MarkWidget(0, 4, 1, 80), new MarkWidget(0, 4, 1, 0)});
        RenderObject* ro = w->createRenderObject();
        delete w;
        ro->layout({0, 100, 0, 20});
        std::vector<RenderObject*> kids;
        collecting = &kids;
        ro->visitChildren(collectChild);
        collecting = nullptr;
        CHECK(kids.size() == 2 && ro->size.width == 100);
        if (kids.size() == 2)
            CHECK(kids[0]->size.width == 80 && kids[1]->size.width == 20);
        delete ro;
    }
    for (int16_t limit : {8000, 8001}) {
        for (bool horizontal : {true, false}) {
            const BoxConstraints k =
                horizontal ? BoxConstraints{0, limit, 0, 20} : BoxConstraints{0, 20, 0, limit};
            const std::vector<std::vector<int> > spec =
                horizontal ? std::vector<std::vector<int> >{{0, 4, 1, 7}}
                           : std::vector<std::vector<int> >{{4, 0, 1, 7}};
            RenderObject* old = horizontal
                                    ? static_cast<RenderObject*>(new ReferenceRow(refs(spec)))
                                    : static_cast<RenderObject*>(new ReferenceColumn(refs(spec)));
            Widget* widget = horizontal ? static_cast<Widget*>(new Row(widgets(spec)))
                                        : static_cast<Widget*>(new Column(widgets(spec)));
            RenderObject* actual = widget->createRenderObject();
            delete widget;
            old->layout(k);
            actual->layout(k);
            if (limit == 8000) {
                CHECK(equalTree(old, actual));
                CHECK((horizontal ? actual->size.width : actual->size.height) == 8000);
            } else {
                CHECK((horizontal ? old->size.width : old->size.height) == 7);
                CHECK((horizontal ? actual->size.width : actual->size.height) == 8001);
                CHECK(static_cast<RenderFlex*>(actual)->overflowMainExtent == 0);
            }
            delete actual;
            delete old;
        }
    }
    // Only the explicit 32767 sentinel is unbounded; finite 8001 must consume
    // available width. This supersedes the former >8000 heuristic.
    {
        Widget* w = new Row({new MarkWidget(0, 4, 1, 7)});
        RenderObject* ro = w->createRenderObject();
        delete w;
        ro->layout({0, BoxConstraints::Unbounded, 0, 20});
        std::vector<RenderObject*> kids;
        collecting = &kids;
        ro->visitChildren(collectChild);
        collecting = nullptr;
        CHECK(kids.size() == 1 && kids[0]->size.width == 7 && ro->size.width == 7);
        delete ro;
    }
    CHECK(std::memcmp(before, after, sizeof before) == 0);
    const char* suffix = NANOUI_DYNAMIC_STORAGE ? "dynamic" : "pool";
    char p[80];
    std::snprintf(p, sizeof p, "flex-before-%s.bmp", suffix);
    writeBmp(before, p);
    std::snprintf(p, sizeof p, "flex-after-%s.bmp", suffix);
    writeBmp(after, p);
}

class OriginalFlexShape final : public RenderObject {
   public:
    NanoUI::EmbeddedVector<RenderObject*> children;
    void performLayout() override {}
    void paintWorker(Canvas*, Point) override {}
};
}  // namespace

int main() {
    MallocAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    const size_t legacyShapeBytes = sizeof(OriginalFlexShape);
    const size_t rowBytes = sizeof(RenderRow), columnBytes = sizeof(RenderColumn);
    CHECK(rowBytes == columnBytes);
    CHECK(rowBytes >= legacyShapeBytes + 2 * sizeof(int32_t));
    run();
    CHECK(heap.live == 0);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf(
        "flex compatibility cases=%d; sizeof RenderRow=%lu RenderColumn=%lu legacy-shape=%lu (+%lu "
        "bytes for content/overflow extents); intentional deltas: minimum 80+20 fits 100 (legacy "
        "130), remainder 16/34/51 (legacy 16/33/50), finite 8001 is bounded (legacy heuristic "
        "treated it unbounded), tight parent size is honored; failures=%d\n",
        casesRun, (unsigned long)rowBytes, (unsigned long)columnBytes,
        (unsigned long)legacyShapeBytes, (unsigned long)(rowBytes - legacyShapeBytes), failures);
    return failures ? 1 : 0;
}
