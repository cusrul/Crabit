// Exercise original product widgets, not the experimental Element framework.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class HeapAdapter final : public NanoUI::MemoryAdapter {
   public:
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};

class PixelCanvas final : public Canvas {
   public:
    unsigned char pixels[128 * 64]{};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= 128 || y >= 64)
            return;
        unsigned char& pixel = pixels[y * 128 + x];
        if (color == C_INVERT)
            pixel ^= 1;
        else
            pixel = color == C_WHITE;
    }
};

namespace {
unsigned testTicks = 0;
unsigned ticks() {
    return testTicks;
}
int pressed = 0;
void press() {
    ++pressed;
}
int failures = 0;
#define CHECK(value)                                        \
    do {                                                    \
        if (!(value)) {                                     \
            ++failures;                                     \
            std::printf("line %d: %s\n", __LINE__, #value); \
        }                                                   \
    } while (false)

// Generated test artifact, not a replacement product App. Use real original
// controls and the built-in ASCII bitmap font; Chinese resources are not tested.
void saveControlsBitmap(const PixelCanvas& canvas) {
    const int scale = 4, width = 128 * scale, height = 64 * scale;
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
    const char* path = NANOUI_DYNAMIC_STORAGE ? "controls-dynamic.bmp" : "controls-pool.bmp";
    FILE* file = std::fopen(path, "wb");
    CHECK(file != nullptr);
    if (!file)
        return;
    CHECK(std::fwrite(header, 1, sizeof header, file) == sizeof header);
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            unsigned char color = canvas.pixels[(y / scale) * 128 + x / scale] ? 255 : 0;
            unsigned char rgb[3] = {color, color, color};
            CHECK(std::fwrite(rgb, 1, 3, file) == 3);
        }
    }
    CHECK(std::fclose(file) == 0);
}

void checkControls(PixelCanvas& canvas) {
    bool enabled = false;
    float level = 5.0f;
    int number = 9;
    Switch toggle(enabled);
    Slider slider(level, 10.0f);
    NumberPicker picker(&number, 0, 9);
    RenderObject* toggleNode = toggle.createRenderObject();
    RenderObject* sliderNode = slider.createRenderObject();
    RenderObject* pickerNode = picker.createRenderObject();
    CHECK(!toggleNode->handleEvent({EVT_OK, 0}) && !enabled);
    toggleNode->hasFocus = true;
    CHECK(toggleNode->handleEvent({EVT_OK, 0}) && enabled);
    sliderNode->hasFocus = true;
    CHECK(!sliderNode->handleEvent({EVT_RIGHT, 0}) && level == 5.0f);
    CHECK(sliderNode->handleEvent({EVT_OK, 0}));
    CHECK(sliderNode->handleEvent({EVT_RIGHT, 0}) && level == 6.0f);
    CHECK(sliderNode->handleEvent({EVT_OK, 0}));
    CHECK(!sliderNode->handleEvent({EVT_LEFT, 0}) && level == 6.0f);
    pickerNode->hasFocus = true;
    CHECK(!pickerNode->handleEvent({EVT_RIGHT, 0}) && number == 9);
    CHECK(pickerNode->handleEvent({EVT_OK, 0}));
    CHECK(pickerNode->handleEvent({EVT_RIGHT, 0}) && number == 0);
    CHECK(pickerNode->handleEvent({EVT_LEFT, 0}) && number == 9);
    CHECK(pickerNode->handleEvent({EVT_OK, 0}));

    toggleNode->hasFocus = sliderNode->hasFocus = pickerNode->hasFocus = false;
    toggleNode->layout(BoxConstraints::loose({128, 64}));
    sliderNode->layout(BoxConstraints::loose({128, 64}));
    pickerNode->layout(BoxConstraints::loose({128, 64}));
    for (int frame = 0; frame < 120; ++frame) {
        testTicks += 17;
        canvas.clear();
        canvas.resetClipRect();
        toggleNode->paint(&canvas, {4, 4});
        sliderNode->paint(&canvas, {40, 7});
        pickerNode->paint(&canvas, {82, 30});
    }
    canvas.drawText(4, 32, "NanoUI", 12, false, 0);
    saveControlsBitmap(canvas);
    delete toggleNode;
    delete sliderNode;
    delete pickerNode;
}
void render(Widget* widget, PixelCanvas& canvas) {
    RenderObject* node = widget->createRenderObject();
    CHECK(node != nullptr);
    if (node) {
        node->layout(BoxConstraints::loose({128, 64}));
        node->paint(&canvas, {0, 0});
        delete node;
    }
    delete widget;
}
}  // namespace

int main() {
    HeapAdapter heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(ticks, nullptr);
    PixelCanvas canvas;
    canvas.resetClipRect();
    render(new Container(new ProgressBar(0.5f, 40, 8), 48, 16, 2, true, 0), canvas);
    CHECK(canvas.pixels[0] == 1 && canvas.pixels[47] == 1);

    Button* button = new Button(new SizedBox(20, 10), press);
    RenderObject* node = button->createRenderObject();
    node->hasFocus = false;
    CHECK(!node->handleEvent({EVT_OK, 0}) && pressed == 0);
    node->hasFocus = true;
    CHECK(node->handleEvent({EVT_OK, 0}) && pressed == 1);
    delete node;
    delete button;

    // Long borrowed text retains the old lifetime contract; no new fixed buffer.
    char longText[161];
    std::memset(longText, 'A', 160);
    longText[160] = 0;
    canvas.clear();
    render(new Text(longText), canvas);
    unsigned lit = 0;
    for (unsigned char pixel : canvas.pixels)
        lit += pixel;
    CHECK(lit > 0);

    const uint8_t bitmap[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
    canvas.clear();
    render(new Image(bitmap, 8, 8), canvas);
    CHECK(canvas.pixels[0] == 1);
    render(new PageTransition(new SizedBox(16, 8)), canvas);
    const uint8_t* icons[] = {bitmap};
    ImageMenu* menu = new ImageMenu(icons, 1);
    node = menu->createRenderObject();
    CHECK(node->handleEvent({EVT_DOWN, 0}));
    delete node;
    delete menu;
    checkControls(canvas);
    CHECK(NanoUI::MemoryPool::instance().getWidgetUsage() == 0);
    CHECK(NanoUI::MemoryPool::instance().getRenderUsage() == 0);
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
#if NANOUI_DYNAMIC_STORAGE
    // Arduino's standard heap remains usable without a product pool adapter.
    render(new Column({new Text("heap"), new SizedBox(10, 5)}), canvas);
    NanoUI::NanoUIText ownedLongText(160, 'A');
    CHECK(ownedLongText.size() == 160);
    CHECK(NanoUI::makeStateKey("shared-prefix-left") !=
          NanoUI::makeStateKey("shared-prefix-right"));
#endif
    std::printf("original widgets failures=%d\n", failures);
    return failures ? 1 : 0;
}
