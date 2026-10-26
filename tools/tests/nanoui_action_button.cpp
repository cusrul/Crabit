// Product-style contract stays outside the standalone NanoUI library tests.
#include "../../firmware/stm32/Core/NanoUI/app_real/components/action_button.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
class Heap final : public NanoUI::MemoryAdapter {
    void* alloc(size_t size) override { return std::malloc(size); }
    void free(void* ptr) override { std::free(ptr); }
};

class Pixels final : public Canvas {
   public:
    unsigned char data[128 * 64]{};
    void clear() override { std::memset(data, 0, sizeof data); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= width() || y >= height())
            return;
        auto& pixel = data[y * width() + x];
        pixel = color == C_INVERT ? !pixel : color == C_WHITE;
    }
};
}  // namespace

int main() {
    Heap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    Pixels canvas;
    canvas.resetClipRect();
    int failures = 0;
    for (const char* label : {" APPLY ", " BACK ", " EXIT "}) {
        int called = 0;
        Widget* oldForm =
            new Button(new Container(new Text(label, 8), 46, 16, 0, true, 2), nullptr);
        Widget* composed = new ActionButton(label, [&called]() { ++called; });
        RenderObject* reference = oldForm->createRenderObject();
        RenderObject* actual = composed->createRenderObject();
        delete oldForm;
        delete composed;  // callback must survive temporary Widget configuration
        const BoxConstraints constraints = BoxConstraints::loose({128, 64});
        reference->layout(constraints);
        actual->layout(constraints);
        canvas.clear();
        reference->paint(&canvas, {8, 8});
        unsigned char expected[sizeof canvas.data];
        std::memcpy(expected, canvas.data, sizeof expected);
        canvas.clear();
        actual->paint(&canvas, {8, 8});
        unsigned ink = 0;
        for (auto pixel : canvas.data)
            ink += pixel != 0;
        if (!ink || std::memcmp(expected, canvas.data, sizeof expected) ||
            actual->size.width != reference->size.width ||
            actual->size.height != reference->size.height ||
            actual->getRadius() != reference->getRadius())
            ++failures;
        actual->hasFocus = true;
        if (!actual->handleEvent({EVT_OK, 0}) || called != 1)
            ++failures;
        std::printf("label=%s size=%dx%d radius=%d ink=%u callback=%d\n", label, actual->size.width,
                    actual->size.height, actual->getRadius(), ink, called);
        delete reference;
        delete actual;
    }
    if (NanoUI::MemoryPool::instance().getWidgetUsage() ||
        NanoUI::MemoryPool::instance().getRenderUsage() ||
        NanoUI::MemoryPool::instance().getVectorUsage())
        ++failures;
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("product action button failures=%d\n", failures);
    return failures ? 1 : 0;
}
