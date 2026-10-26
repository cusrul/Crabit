// Windows-only design candidates. No board service, SDK or firmware entry.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include "watch_pages.h"
#include "animated_faces.h"
#include "app_pages.h"

int previewFontSize = 8;
bool previewChinese = true;
#include "app_settings_preview.h"

extern "C" void OLED_WR_Byte(uint8_t, uint8_t) {
}
namespace {
uint32_t ticks = 1000;
uint32_t getTicks() {
    return ticks;
}
class Heap : public NanoUI::MemoryAdapter {
   public:
    void* alloc(size_t bytes) override { return std::malloc(bytes); }
    void free(void* pointer) override { std::free(pointer); }
};
class Input : public InputDriver {
   public:
    void init() override {}
    UIEvent poll() override { return {EVT_NONE, 0}; }
};
class BitmapCanvas : public Canvas {
    uint8_t pixels[128 * 64] = {};

   public:
    void clear() override { memset(pixels, 0, sizeof(pixels)); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || x >= 128 || y < 0 || y >= 64)
            return;
        auto& pixel = pixels[y * 128 + x];
        if (color == C_INVERT)
            pixel ^= 1;
        else
            pixel = color == C_WHITE;
    }
    void save(const std::string& path) {
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file)
            std::exit(2);
        std::fprintf(file, "P5\n128 64\n255\n");
        unsigned int lit = 0;
        for (auto pixel : pixels) {
            std::fputc(pixel ? 255 : 0, file);
            lit += pixel != 0;
        }
        std::fclose(file);
        if (lit < 40) {
            std::fprintf(stderr, "blank preview: %s\n", path.c_str());
            std::exit(3);
        }
    }
};
void validateLayout(RenderObject* object) {
    if (auto* flex = dynamic_cast<RenderFlex*>(object)) {
        if (flex->overflowMainExtent != 0) {
            std::fprintf(stderr, "candidate flex overflow: %ld\n", long(flex->overflowMainExtent));
            std::exit(4);
        }
    }
    if (auto* text = dynamic_cast<RenderText*>(object)) {
        if (text->fullTextWidth > text->size.width || text->textSize > text->size.height) {
            std::fprintf(stderr, "candidate text clipped: %s\n", text->text);
            std::exit(5);
        }
    }
    object->visitChildren(validateLayout);
}
void render(const std::string& output, const char* id, Widget* widget) {
    auto* object = widget->createRenderObject();
    object->layout({128, 128, 64, 64});
    validateLayout(object);
    BitmapCanvas canvas;
    canvas.resetClipRect();
    object->paint(&canvas, {0, 0});
    canvas.save(output + "/" + id + ".pgm");
    delete object;
    delete widget;
}
void settings(const std::string& output, int size, bool chinese, int focus, const char* id) {
    previewFontSize = size;
    previewChinese = chinese;
    SettingsApp app;
    app.onCreate();
    BitmapCanvas canvas;
    Input input;
    WindowManager manager(&canvas, &input);
    manager.push(app.buildUI());
    for (int i = 0; i < 200; ++i) {
        ticks += 16;
        manager.update();
        manager.drawFrame();
    }
    for (int i = 0; i < focus; ++i) {
        manager.dispatchEvent({EVT_DOWN, 0});
        for (int frame = 0; frame < 50; ++frame) {
            ticks += 16;
            manager.update();
            manager.drawFrame();
        }
    }
    manager.markDirty();
    manager.update();
    manager.drawFrame();
    canvas.save(output + "/" + id + ".pgm");
    manager.clear();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc != 2)
        return 1;
    Heap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    HAL_Time::init(getTicks, nullptr);
    const std::string output(argv[1]);
    settings(output, 8, false, 0, "settings-en");
    settings(output, 8, true, 0, "settings-8-top");
    settings(output, 10, true, 0, "settings-10-top");
    settings(output, 8, true, 4, "settings-8-bottom");
    settings(output, 10, true, 4, "settings-10-bottom");
    settings(output, 8, true, 6, "settings-8-actions");
    settings(output, 10, true, 6, "settings-10-actions");
    for (int font : {8, 10, 12}) {
        for (int face = 0; face < 4; ++face) {
            std::string id = "face-" + std::to_string(face) + "-" + std::to_string(font);
            render(output, id.c_str(), CrabitUiReview::buildFace(face, font));
        }
        for (int style = 0; style < 2; ++style) {
            std::string id = "weather-" + std::to_string(style) + "-" + std::to_string(font);
            render(output, id.c_str(), CrabitUiReview::buildWeather(style, font, false, false));
        }
    }
    render(output, "weather-stale", CrabitUiReview::buildWeather(0, 10, true, false));
    render(output, "weather-empty", CrabitUiReview::buildWeather(0, 10, false, true));
    render(output, "weather-selected-8", CrabitUiReview::buildWeather(0, 8, false, false));
    for (int variant = 0; variant < 4; ++variant) {
        const std::string id = "tools-" + std::to_string(variant) + "-8";
        render(output, id.c_str(), CrabitUiReview::buildToolsPage(variant));
    }
    for (int variant = 0; variant < 2; ++variant) {
        // Hold readable endpoints, then run a short transition in each direction.
        for (int frame = 0; frame < 80; ++frame) {
            float phase = frame < 20   ? 0.0f
                          : frame < 40 ? (frame - 20) / 19.0f
                          : frame < 60 ? 1.0f
                                       : (79 - frame) / 19.0f;
            char id[64];
            std::snprintf(id, sizeof(id), "motion-%d-%03d", variant, frame);
            render(output, id, CrabitUiReview::buildAnimatedFace(variant, phase));
        }
    }
    std::puts("Static and animated NanoUI candidates rendered; production defaults unchanged.");
}
