#include "../../firmware/shared/ui/animated_time.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace Crabit::WatchFaces;

uint8_t glyphs[128][12];
uint32_t currentTick = 0;
int failures = 0;

class Heap final : public NanoUI::MemoryAdapter {
   public:
    size_t allocations = 0;
    void* alloc(size_t size) override {
        ++allocations;
        return std::malloc(size);
    }
    void free(void* ptr) override { std::free(ptr); }
};

Heap heap;

class Pixels final : public Canvas {
   public:
    uint8_t data[128 * 64] = {};
    bool outOfBounds = false;

    void clear() override {
        std::memset(data, 0, sizeof(data));
        outOfBounds = false;
    }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= width() || y < 0 || y >= height()) {
            outOfBounds = true;
            return;
        }
        data[y * width() + x] = color == C_WHITE;
    }
};

struct RenderedSize {
    int16_t width;
    int16_t height;
};

void check(bool condition, const char* label) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++failures;
    }
}

uint32_t hashPixels(const Pixels& pixels) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(pixels.data); ++i) {
        hash ^= pixels.data[i];
        hash *= 16777619u;
    }
    return hash;
}

unsigned inkPixels(const Pixels& pixels) {
    unsigned ink = 0;
    for (size_t i = 0; i < sizeof(pixels.data); ++i)
        ink += pixels.data[i] != 0;
    return ink;
}

RenderedSize renderWidget(Widget& widget, Pixels& pixels, Point origin, bool resetClip = true) {
    if (resetClip)
        pixels.resetClipRect();
    RenderObject* render = widget.createRenderObject();
    check(render != nullptr, "widget creates a render object");
    if (!render)
        return {0, 0};
    render->layout(BoxConstraints::loose({128, 64}));
    const RenderedSize size = {render->size.width, render->size.height};
    const size_t allocationsBeforePaint = heap.allocations;
    render->paint(&pixels, origin);
    check(heap.allocations == allocationsBeforePaint, "paint performs no heap allocation");
    check(!pixels.outOfBounds, "paint keeps pixel coordinates in bounds");
    delete render;
    return size;
}

void renderLegacy(bool minimal, float phase, const TimeTransition& state, Pixels& pixels,
                  Point origin, RenderedSize* size) {
    pixels.resetClipRect();
    RenderAnimatedTime render(minimal, &state, phase);
    render.layout(BoxConstraints::loose({128, 64}));
    render.paint(&pixels, origin);
    if (size)
        *size = {render.size.width, render.size.height};
    check(!pixels.outOfBounds, "legacy paint keeps pixel coordinates in bounds");
}

uint32_t mockTicks() {
    return currentTick;
}
void mockDelay(uint32_t) {
}

void checkSize(const char* label, Widget& widget, int16_t width, int16_t height) {
    Pixels pixels;
    const RenderedSize size = renderWidget(widget, pixels, {0, 0});
    if (size.width != width || size.height != height) {
        std::fprintf(stderr, "FAIL: %s expected=%dx%d actual=%dx%d\n", label, width, height,
                     size.width, size.height);
        ++failures;
    }
}

bool equalsCrop(const Pixels& source, const Pixels& slice, int sourceX, int originX, int originY,
                int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int sourceIndex = (originY + y) * 128 + sourceX + x;
            const int sliceIndex = (originY + y) * 128 + originX + x;
            if (source.data[sourceIndex] != slice.data[sliceIndex])
                return false;
        }
    }
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 128; ++x) {
            if (x >= originX && x < originX + width && y >= originY && y < originY + height)
                continue;
            if (slice.data[y * 128 + x])
                return false;
        }
    }
    return true;
}

bool equalsTightLegacy(const Pixels& legacy, const Pixels& tight, int originX, int originY,
                       int width, int height) {
    const int legacyTop = originY + (kTimeHeight - kBitmapHeight) / 2;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            if (legacy.data[(legacyTop + y) * 128 + originX + x] !=
                tight.data[(originY + y) * 128 + originX + x])
                return false;
    return true;
}

}  // namespace

uint32_t decode_utf8(const char** input) {
    if (!input || !*input || !**input)
        return 0;
    return static_cast<uint8_t>(*(*input)++);
}

const uint8_t* get_chinese_glyph(uint32_t codepoint, uint8_t size, uint8_t* width, uint8_t) {
    if (codepoint >= 128 || size != 12 || !width)
        return nullptr;
    *width = 6;
    for (uint8_t row = 0; row < 12; ++row)
        glyphs[codepoint][row] = static_cast<uint8_t>((codepoint * 17u + row * 13u) & 0xFCu);
    return glyphs[codepoint];
}

int16_t get_utf8_string_width(const char* text, uint8_t, uint8_t) {
    return static_cast<int16_t>(std::strlen(text) * 6);
}

int main() {
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(mockTicks, mockDelay);

    TimeTransition stable;
    stable.set(12, 34, 0, false);
    Pixels legacyStable;
    RenderedSize legacySize = {0, 0};
    renderLegacy(true, 1.0f, stable, legacyStable, {17, 13}, &legacySize);
    check(legacySize.width == 90 && legacySize.height == 36,
          "legacy minimal RenderAnimatedTime default size remains 90x36");
    check(hashPixels(legacyStable) == 0x8734b236u && inkPixels(legacyStable) == 513,
          "legacy minimal stable pixel baseline");
    std::printf("legacy minimal stable: 90x36 ink=%u hash=%08lx\n", inkPixels(legacyStable),
                static_cast<unsigned long>(hashPixels(legacyStable)));

    AnimatedTimeDisplay legacyWidget(true, &stable);
    Pixels legacyWidgetPixels;
    const RenderedSize legacyWidgetSize = renderWidget(legacyWidget, legacyWidgetPixels, {17, 13});
    check(legacyWidgetSize.width == 90 && legacyWidgetSize.height == 36,
          "AnimatedTimeDisplay default size remains 90x36");
    check(hashPixels(legacyWidgetPixels) == 0x8734b236u,
          "AnimatedTimeDisplay minimal default pixels remain unchanged");

    TimeTransition moving;
    moving.set(12, 34, 100, false);
    moving.set(12, 35, 100, true);
    Pixels legacyMid;
    renderLegacy(true, 0.5f, moving, legacyMid, {17, 13}, nullptr);
    check(hashPixels(legacyMid) == 0xd1dd08eeu && inkPixels(legacyMid) == 543,
          "legacy minimal transition pixel baseline");
    std::printf("legacy minimal transition: ink=%u hash=%08lx\n", inkPixels(legacyMid),
                static_cast<unsigned long>(hashPixels(legacyMid)));

    Pixels legacyDotMatrix;
    RenderedSize dotMatrixSize = {0, 0};
    renderLegacy(false, 1.0f, stable, legacyDotMatrix, {17, 13}, &dotMatrixSize);
    check(dotMatrixSize.width == 90 && dotMatrixSize.height == 36,
          "legacy dot-matrix RenderAnimatedTime default size remains 90x36");
    check(hashPixels(legacyDotMatrix) == 0x22b415b5u && inkPixels(legacyDotMatrix) == 736,
          "legacy dot-matrix pixel baseline");

    MinimalTimeDigits full(&stable);
    MinimalTimeDigits halfScale(&stable, 2);
    MinimalTimeDigits left(&stable, 3, 0, 2);
    MinimalTimeDigits right(&stable, 3, 3, 2);
    checkSize("scale 3 full slice is 90x21", full, 90, 21);
    checkSize("scale 2 full slice is 60x14", halfScale, 60, 14);
    checkSize("left two scale 3 slots are 36x21", left, 36, 21);
    checkSize("right two scale 3 slots are 36x21", right, 36, 21);

    Pixels fullPixels;
    Pixels leftPixels;
    Pixels rightPixels;
    const Point sliceOrigin = {4, 6};
    renderWidget(full, fullPixels, sliceOrigin);
    renderWidget(left, leftPixels, sliceOrigin);
    renderWidget(right, rightPixels, sliceOrigin);
    check(equalsCrop(fullPixels, leftPixels, sliceOrigin.x, sliceOrigin.x, sliceOrigin.y, 36, 21),
          "left slice equals full-face first two slots and clips to its frame");
    check(equalsCrop(fullPixels, rightPixels, sliceOrigin.x + 3 * 18, sliceOrigin.x, sliceOrigin.y,
                     36, 21),
          "right slice equals full-face last two slots and clips to its frame");

    MinimalTimeDigits clampScaleLow(&stable, 0, 4, 0);
    MinimalTimeDigits clampScaleHigh(&stable, 255, 0, 255);
    MinimalTimeDigits clampFirst(&stable, 3, 255, 255);
    MinimalTimeDigits clampCount(&stable, 3, 4, 0);
    checkSize("scale clamps to 1 and zero count to one remaining slot", clampScaleLow, 6, 7);
    checkSize("scale clamps to 3 and count to available slots", clampScaleHigh, 90, 21);
    checkSize("first slot clamps to 4 and count to one", clampFirst, 18, 21);
    checkSize("zero count clamps to one", clampCount, 18, 21);
    MinimalTimeDigits lastDigit(&stable, 3, 4, 1);
    Pixels lastDigitPixels;
    renderWidget(lastDigit, lastDigitPixels, sliceOrigin);
    check(equalsCrop(fullPixels, lastDigitPixels, sliceOrigin.x + 4 * 18, sliceOrigin.x,
                     sliceOrigin.y, 18, 21),
          "last legal source slot reads only previous/current index 4");

    for (unsigned endpoint = 0; endpoint < 3; ++endpoint) {
        const uint32_t ticks = endpoint == 0 ? 100 : (endpoint == 1 ? 310 : 520);
        const float phase = endpoint == 0 ? 0.0f : (endpoint == 1 ? 0.5f : 1.0f);
        currentTick = ticks;
        Pixels oldPixels;
        Pixels tightPixels;
        renderLegacy(true, phase, moving, oldPixels, {9, 7}, nullptr);
        MinimalTimeDigits animated(&moving);
        renderWidget(animated, tightPixels, {9, 7});
        check(equalsTightLegacy(oldPixels, tightPixels, 9, 7, 90, 21),
              endpoint == 0 ? "slice transition start matches legacy roll"
                            : (endpoint == 1 ? "slice transition midpoint matches legacy roll"
                                             : "slice transition end matches legacy roll"));
    }

    Pixels clipped;
    clipped.setClipRect({20, 20, 30, 30});
    const Rect priorClip = clipped.getClipRect();
    renderWidget(full, clipped, {0, 0}, false);
    check(inkPixels(clipped) <= 100, "canvas frame clipping limits visible pixels");
    check(clipped.getClipRect().left == priorClip.left &&
              clipped.getClipRect().top == priorClip.top &&
              clipped.getClipRect().right == priorClip.right &&
              clipped.getClipRect().bottom == priorClip.bottom,
          "frame clip is restored after painting");

    Pixels saturated;
    renderWidget(full, saturated, {32760, 32760});
    check(inkPixels(saturated) == 0 && !saturated.outOfBounds,
          "positive saturated origin remains clipped safely");
    renderWidget(full, saturated, {-32760, -32760});
    check(inkPixels(saturated) == 0 && !saturated.outOfBounds,
          "negative saturated origin remains clipped safely");

    MinimalTimeDigits nullState(nullptr);
    Pixels nullPixels;
    checkSize("null state remains a bounded blank slice", nullState, 90, 21);
    renderWidget(nullState, nullPixels, {0, 0});
    check(inkPixels(nullPixels) == 0, "null state paints no pixels");

    check(NanoUI::MemoryPool::instance().getRenderUsage() == 0,
          "all temporary render objects are released");
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("minimal time slices failures=%d\n", failures);
    return failures ? 1 : 0;
}
