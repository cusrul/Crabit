// Batched leaf-widget layout contract. This test intentionally targets the
// original Widget/RenderObject implementation, not the experimental framework.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class HeapAdapter final : public NanoUI::MemoryAdapter {
   public:
    size_t allocations = 0;
    size_t releases = 0;
    void* alloc(size_t size) override {
        void* ptr = std::malloc(size);
        if (ptr)
            ++allocations;
        return ptr;
    }
    void free(void* ptr) override {
        if (ptr) {
            ++releases;
            std::free(ptr);
        }
    }
};

class LeafCanvas final : public Canvas {
   public:
    uint8_t pixels[48 * 32]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 48; }
    int16_t height() override { return 32; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && y >= 0 && x < width() && y < height())
            pixels[y * width() + x] = color == C_WHITE;
    }
};

namespace {
int failures = 0;
#define CHECK(expr)                                        \
    do {                                                   \
        if (!(expr)) {                                     \
            ++failures;                                    \
            std::printf("line %d: %s\n", __LINE__, #expr); \
        }                                                  \
    } while (false)

void layout(RenderObject* node, BoxConstraints constraints, Size expected) {
    CHECK(node != nullptr);
    if (!node)
        return;
    node->layout(constraints);
    CHECK(node->size.width == expected.width);
    CHECK(node->size.height == expected.height);
}

void changed(bool value) {
    (void)value;
}
void changed(float value) {
    (void)value;
}

void testDimensions() {
    const uint8_t bitmap[64] = {};
    const uint8_t animation[12] = {0x55, 0x4e, 1, 1, 8, 0, 8, 0, 1, 0, 1, 0};
    RenderText text("abcdefgh", 12, false);
    RenderImage image(bitmap, 8, 8);
    RenderAnimatedImage animated(animation, true);
    bool enabled = false;
    float value = 0.5f;
    int number = 3;
    RenderSwitch toggle(&enabled, nullptr);
    RenderSlider slider(&value, 1.0f, 30, nullptr);
    RenderNumberPicker picker(&number, 0, 9, "%d");
    RenderProgressBar progress(0.5f, 30, 8);
    RenderButton emptyButton(nullptr);

    layout(&text, BoxConstraints::loose({128, 64}), {48, 12});
    layout(&text, BoxConstraints::tight(7, 5), {7, 5});
    layout(&text, BoxConstraints::loose({0, 0}), {0, 0});
    layout(&image, BoxConstraints::loose({128, 64}), {8, 8});
    layout(&image, BoxConstraints::tight(3, 2), {3, 2});
    layout(&animated, BoxConstraints::tight(4, 6), {4, 6});
    layout(&toggle, BoxConstraints::tight(2, 3), {2, 3});
    layout(&slider, BoxConstraints::tight(3, 2), {3, 2});
    layout(&picker, BoxConstraints::loose({0, 0}), {0, 0});
    layout(&progress, BoxConstraints::tight(1, 1), {1, 1});
    layout(&emptyButton, BoxConstraints::tight(7, 5), {7, 5});

    RenderImage missing(nullptr, 12, 12);
    layout(&missing, BoxConstraints::tight(4, 5), {4, 5});
    RenderAnimatedImage invalid(nullptr, true);
    layout(&invalid, BoxConstraints::tight(4, 5), {4, 5});

    CHECK(BoxConstraints::loose({20, 10}).contains(image.size));

    Text* ownedWidget = new Text("owned");
    RenderObject* ownedRender = ownedWidget->createRenderObject();
    CHECK(ownedRender != nullptr);
    delete ownedRender;
    delete ownedWidget;
}

void testDynamicTextLayout() {
    NanoUI::NanoUIText source("a");
    RenderSlidingText sliding(&source, 12);
    sliding.layout(BoxConstraints::loose({128, 64}));
    const int16_t initial = sliding.size.width;
    source = "a much longer string";
    LeafCanvas canvas;
    sliding.paint(&canvas, {0, 0});
    CHECK(sliding.needsLayout);
    sliding.layout(BoxConstraints::loose({128, 64}));
    CHECK(sliding.size.width > initial);

    NanoUI::NanoUIText clock("1");
    RenderFlipClockText flip(&clock, 12);
    flip.layout(BoxConstraints::loose({128, 64}));
    const int16_t clockInitial = flip.size.width;
    clock = "123456";
    flip.paint(&canvas, {0, 0});
    CHECK(flip.needsLayout);
    flip.layout(BoxConstraints::loose({128, 64}));
    CHECK(flip.size.width > clockInitial);
}

void testClipAndEvents() {
    LeafCanvas canvas;
    canvas.resetClipRect();
    const Rect ancestor = {4, 3, 18, 17};
    canvas.setClipRect(ancestor);
    {
        ScopedClip clip(&canvas, {0, 0, 12, 12});
        Rect nested = canvas.getClipRect();
        CHECK(nested.left == 4 && nested.top == 3 && nested.right == 12 && nested.bottom == 12);
    }
    Rect restored = canvas.getClipRect();
    CHECK(restored.left == ancestor.left && restored.top == ancestor.top &&
          restored.right == ancestor.right && restored.bottom == ancestor.bottom);

    bool enabled = false;
    RenderSwitch toggle(&enabled, nullptr);
    toggle.hasFocus = true;
    CHECK(toggle.handleEvent({EVT_OK, 0}) && enabled);
    float value = 0.5f;
    RenderSlider slider(&value, 1.0f, 30, changed);
    slider.hasFocus = true;
    CHECK(slider.handleEvent({EVT_OK, 0}));
    CHECK(slider.handleEvent({EVT_RIGHT, 0}));
    int number = 9;
    RenderNumberPicker picker(&number, 0, 9, "%d");
    picker.hasFocus = true;
    CHECK(picker.handleEvent({EVT_OK, 0}));
    CHECK(picker.handleEvent({EVT_RIGHT, 0}) && number == 0);

    RenderProgressBar zero(0.5f, 0, 0);
    zero.layout(BoxConstraints::tight(0, 0));
    zero.paint(&canvas, {4, 3});
    RenderSlider zeroRange(&value, 0.0f, 0, nullptr);
    zeroRange.layout(BoxConstraints::tight(0, 0));
    zeroRange.paint(&canvas, {4, 3});
}
}  // namespace

int main() {
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    testDimensions();
    testDynamicTextLayout();
    testClipAndEvents();
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getVectorUsage() == 0);
    CHECK(heap.allocations == heap.releases);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("layout leaf contract failures=%d\n", failures);
    return failures ? 1 : 0;
}
