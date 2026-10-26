// Current behavior checks, not identity with the pre-layout implementation.
#include <NanoUI.h>
#include <cstdio>
#include <cstring>

namespace {
int failures = 0;
uint32_t now = 0;
uint32_t ticks() {
    return now;
}
void delayMs(uint32_t) {
}
#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

class Pixels final : public Canvas {
   public:
    enum { W = 40, H = 32 };
    uint8_t pixels[W * H]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return W; }
    int16_t height() override { return H; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x >= 0 && y >= 0 && x < W && y < H) {
            uint8_t& pixel = pixels[y * W + x];
            if (color == C_INVERT)
                pixel ^= 1;
            else
                pixel = color == C_WHITE;
        }
    }
    bool at(int x, int y) const { return pixels[y * W + x] != 0; }
    void checkOutside(Rect clip) const {
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                if (x < clip.left || x >= clip.right || y < clip.top || y >= clip.bottom)
                    CHECK(!at(x, y));
            }
    }
};

void checkAnimatedImage() {
    Pixels canvas;
    canvas.resetClipRect();
    // Two 2x9 vertical-page frames, including the partial second page.
    alignas(AssetHeader) const uint8_t frames[] = {0x55, 0x4e, 1, 2,   2, 0, 9, 0,  2, 0,
                                                   10,   0,    1, 128, 1, 0, 2, 64, 0, 1};
    now = 0;
    RenderAnimatedImage looping(frames, true);
    looping.layout(BoxConstraints::loose({40, 32}));
    CHECK(looping.size.width == 2 && looping.size.height == 9);
    looping.paint(&canvas, {3, 4});
    CHECK(canvas.at(3, 4) && canvas.at(3, 12) && !canvas.at(4, 12));
    canvas.clear();
    now = 100;
    looping.paint(&canvas, {3, 4});
    CHECK(!canvas.at(3, 4) && canvas.at(3, 5) && canvas.at(4, 12));
    canvas.clear();
    now = 200;
    looping.paint(&canvas, {3, 4});
    CHECK(canvas.at(3, 4) && !canvas.at(3, 5));  // wraps to frame zero

    now = 0;
    RenderAnimatedImage once(frames, false);
    once.layout(BoxConstraints::loose({40, 32}));
    canvas.clear();
    now = 1000;
    once.paint(&canvas, {3, 4});
    CHECK(!canvas.at(3, 4) && canvas.at(3, 5) && canvas.at(4, 12));
    canvas.clear();
    now = 2000;
    once.paint(&canvas, {3, 4});
    CHECK(!canvas.at(3, 4) && canvas.at(3, 5));  // stays on final frame
    once.play();
    canvas.clear();
    once.paint(&canvas, {3, 4});
    CHECK(canvas.at(3, 4) && !canvas.at(3, 5));

    alignas(AssetHeader) uint8_t fast[sizeof frames];
    std::memcpy(fast, frames, sizeof frames);
    fast[10] = 0xd0;
    fast[11] = 0x07;  // 2000fps: at least 1ms/frame
    now = 0;
    RenderAnimatedImage highFps(fast, true);
    highFps.layout(BoxConstraints::loose({40, 32}));
    canvas.clear();
    now = 1;
    highFps.paint(&canvas, {3, 4});
    CHECK(!canvas.at(3, 4) && canvas.at(3, 5));
    fast[8] = fast[9] = 0;  // zero-frame metadata must not divide by zero
    now = 0;
    RenderAnimatedImage noFrames(fast, true);
    noFrames.layout(BoxConstraints::loose({40, 32}));
    canvas.clear();
    now = 100;
    noFrames.paint(&canvas, {3, 4});
    CHECK(canvas.at(3, 4));  // existing fallback uses the first page data

    canvas.clear();
    now = 0;
    const Rect ancestor{3, 4, 4, 8};
    canvas.setClipRect(ancestor);
    looping.paint(&canvas, {3, 4});
    CHECK(canvas.at(3, 4));
    canvas.checkOutside(ancestor);
    const Rect restored = canvas.getClipRect();
    CHECK(std::memcmp(&ancestor, &restored, sizeof ancestor) == 0);
}

void checkImageMenu() {
    Pixels canvas;
    canvas.resetClipRect();
    const uint8_t solid[4] = {0xf0, 0xf0, 0xf0, 0xf0};
    const uint8_t blank[4] = {};
    const uint8_t* icons[] = {solid, blank};
    RenderImageMenu menu(icons, 2);
    menu.iconSize = 4;
    menu.spacing = 2;
    menu.layout(BoxConstraints::tight(20, 12));
    CHECK(menu.size.width == 20 && menu.size.height == 12);
    const Rect ancestor{5, 5, 17, 12};
    canvas.setClipRect(ancestor);
    menu.currentScrollX = 8;  // settled position for item zero
    menu.paint(&canvas, {3, 4});
    CHECK(canvas.at(11, 5));
    canvas.checkOutside(ancestor);
    const Rect restored = canvas.getClipRect();
    CHECK(std::memcmp(&ancestor, &restored, sizeof ancestor) == 0);

    CHECK(menu.handleEvent({EVT_DOWN, 0}) && menu.selectedIndex == 1);
    canvas.clear();
    menu.currentScrollX = 2;  // settled position for item one
    menu.paint(&canvas, {3, 4});
    CHECK(canvas.at(5, 5) && !canvas.at(11, 5));
    canvas.checkOutside(ancestor);
    CHECK(menu.handleEvent({EVT_DOWN, 0}) && menu.selectedIndex == 0);
    CHECK(menu.handleEvent({EVT_UP, 0}) && menu.selectedIndex == 1);
    CHECK(!menu.handleEvent({EVT_OK, 0}));
    menu.selectedIndex = 99;
    menu.markNeedsLayout();
    menu.layout(BoxConstraints::tight(20, 12));
    CHECK(menu.selectedIndex == 1);

    RenderImageMenu empty(nullptr, 0);
    empty.layout(BoxConstraints::tight(0, 0));
    CHECK(!empty.handleEvent({EVT_UP, 0}) && empty.selectedIndex == 0);
    CHECK(!empty.handleEvent({EVT_DOWN, 0}));
    canvas.clear();
    empty.paint(&canvas, {3, 4});
    for (int i = 0; i < Pixels::W * Pixels::H; ++i)
        CHECK(canvas.pixels[i] == 0);
}

void checkSmallProgressBounds() {
    Pixels canvas;
    canvas.resetClipRect();
    RenderProgressBar progress(2.0f, 20, 8);
    progress.layout(BoxConstraints::tight(6, 4));
    const Rect clip{2, 3, 8, 7};
    canvas.setClipRect(clip);
    progress.paint(&canvas, {2, 3});
    CHECK(canvas.at(3, 4) && canvas.at(6, 5));  // clamped full interior
    canvas.checkOutside(clip);
    progress.progress = -1.0f;
    canvas.clear();
    progress.paint(&canvas, {2, 3});
    CHECK(canvas.at(2, 3) && !canvas.at(3, 4));  // border only
    progress.layout(BoxConstraints::tight(0, 0));
    canvas.clear();
    progress.paint(&canvas, {2, 3});
    for (int i = 0; i < Pixels::W * Pixels::H; ++i)
        CHECK(canvas.pixels[i] == 0);
}
}  // namespace

int main() {
    HAL_Time::init(ticks, delayMs);
    checkAnimatedImage();
    checkImageMenu();
    checkSmallProgressBounds();
    std::printf("layout asset contract failures=%d\n", failures);
    return failures ? 1 : 0;
}
