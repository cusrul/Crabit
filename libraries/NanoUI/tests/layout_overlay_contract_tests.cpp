// Focused contracts for original overlay/composite widgets.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class OverlayHeap final : public NanoUI::MemoryAdapter {
   public:
    size_t live = 0;
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

class OverlayCanvas final : public Canvas {
   public:
    static const int W = 160, H = 100;
    unsigned char pixels[W * H]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return W; }
    int16_t height() override { return H; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= W || y >= H)
            return;
        pixels[y * W + x] = color == C_WHITE;
    }
};

namespace {
int failures = 0;
#define CHECK(x)                                        \
    do {                                                \
        if (!(x)) {                                     \
            ++failures;                                 \
            std::printf("line %d: %s\n", __LINE__, #x); \
        }                                               \
    } while (false)
unsigned fakeTicks = 0;
unsigned getTicks() {
    return fakeTicks;
}

class LayoutProbe final : public RenderObject {
   public:
    Size desired;
    Point painted = {0, 0};
    explicit LayoutProbe(Size s) : desired(s) {}
    void performLayout() override { size = constraints.constrainSize(desired); }
    void paintWorker(Canvas* c, Point p) override {
        painted = p;
        c->fillRect(p.x, p.y, size.width, size.height, C_WHITE);
    }
};

void checkCarousel(OverlayCanvas& canvas) {
    RenderCarousel empty(4, NanoUI::StateKey{});
    empty.layout(BoxConstraints::tight(11, 9));
    CHECK(empty.size.width == 11 && empty.size.height == 9);
    CHECK(!empty.handleEvent({EVT_RIGHT, 0}));
    empty.paint(&canvas, {3, 5});

    RenderCarousel carousel(4, NanoUI::StateKey{});
    LayoutProbe* a = new LayoutProbe({30, 10});
    LayoutProbe* b = new LayoutProbe({35, 14});
    a->parent = &carousel;
    b->parent = &carousel;
    carousel.children.push_back(a);
    carousel.children.push_back(b);
    carousel.selectedIndex = 100;
    carousel.layout(BoxConstraints::tight(20, 8));
    CHECK(carousel.size.width == 20 && carousel.size.height == 8);
    CHECK(carousel.selectedIndex == 1);

    canvas.clear();
    canvas.resetClipRect();
    canvas.setClipRect({10, 10, 25, 25});
    Rect outer = canvas.getClipRect();
    carousel.paint(&canvas, {15, 15});
    Rect after = canvas.getClipRect();
    CHECK(std::memcmp(&outer, &after, sizeof(Rect)) == 0);
    for (int y = 0; y < OverlayCanvas::H; ++y)
        for (int x = 0; x < OverlayCanvas::W; ++x)
            if (x < 10 || x >= 25 || y < 10 || y >= 25)
                CHECK(canvas.pixels[y * OverlayCanvas::W + x] == 0);
    // Carousel owns both dynamically allocated probes until its destructor.
}

void checkImageMenu(OverlayCanvas& canvas) {
    RenderImageMenu empty(nullptr, 0);
    empty.layout(BoxConstraints::loose({13, 4}));
    CHECK(empty.size.width == 13 && empty.size.height == 4);
    CHECK(!empty.handleEvent({EVT_DOWN, 0}));
    empty.paint(canvas.width() ? &canvas : nullptr, {8, 7});
}

void checkTransition(OverlayCanvas& canvas) {
    RenderPageTransition transition(TransitionType::CRT_Expand);
    LayoutProbe* content = new LayoutProbe({40, 22});
    transition.child = content;
    content->parent = &transition;
    transition.layout(BoxConstraints::tight(40, 22));
    CHECK(transition.size.width == 40 && transition.size.height == 22);
    canvas.clear();
    canvas.resetClipRect();
    canvas.setClipRect({12, 9, 30, 25});
    Rect before = canvas.getClipRect();
    transition.paint(&canvas, {10, 8});
    const Rect after = canvas.getClipRect();
    CHECK(std::memcmp(&before, &after, sizeof(Rect)) == 0);
}

void checkPopup(OverlayCanvas& canvas) {
    RenderPopupRoot root;
    Widget* pending = new SizedBox(14, 11);
    Popup::show(pending, 14, 11);
    root.layout(BoxConstraints::tight(36, 26));
    CHECK(root.size.width == 36 && root.size.height == 26);
    CHECK(root.popup && root.popup->size.width == 14 && root.popup->size.height == 11);
    const Size contentSize = root.popup->size;
    root.markNeedsLayout();
    root.layout(BoxConstraints::tight(36, 26));
    CHECK(root.popup->size.width == contentSize.width &&
          root.popup->size.height == contentSize.height);
    CHECK(root.popupFrame.width == contentSize.width + 6 &&
          root.popupFrame.height == contentSize.height + 6);
    Popup::hide();
    root.markNeedsLayout();
    root.layout(BoxConstraints::tight(4, 3));
    delete pending;  // Borrowed configuration outlives its RenderObject.
    CHECK(root.size.width == 4 && root.size.height == 3 && root.popup == nullptr);

    Widget* tiny = new SizedBox(15, 12);
    Popup::show(tiny, 15, 12);
    root.markNeedsLayout();
    root.layout(BoxConstraints::tight(3, 2));
    CHECK(root.size.width == 3 && root.size.height == 2);
    CHECK(root.popup && root.popup->size.width <= 0 && root.popup->size.height <= 0);
    Popup::hide();
    root.markNeedsLayout();
    root.layout(BoxConstraints::tight(3, 2));
    delete tiny;
    CHECK(root.popup == nullptr);
    canvas.clear();
    canvas.resetClipRect();
    canvas.setClipRect({20, 20, 24, 23});
    Rect oldClip = canvas.getClipRect();
    root.paint(&canvas, {18, 19});
    Rect newClip = canvas.getClipRect();
    CHECK(std::memcmp(&oldClip, &newClip, sizeof(Rect)) == 0);
}
}  // namespace

int main() {
    OverlayHeap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    OverlayCanvas canvas;
    checkCarousel(canvas);
    checkImageMenu(canvas);
    checkTransition(canvas);
    checkPopup(canvas);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(heap.live == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("layout overlay failures=%d\n", failures);
    return failures ? 1 : 0;
}
