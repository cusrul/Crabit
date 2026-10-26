// Pixel oracle copied from Container at baseline a770e26. It deliberately
// retains the old constraint bug; compare only cases with valid old results.
#include <NanoUI.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

class Heap final : public NanoUI::MemoryAdapter {
   public:
    int outstanding = 0;
    void* alloc(size_t n) override {
        void* p = std::malloc(n);
        if (p)
            ++outstanding;
        return p;
    }
    void free(void* p) override {
        if (p)
            --outstanding;
        std::free(p);
    }
};

class Pixels final : public Canvas {
   public:
    unsigned char data[128 * 64]{};
    void clear() override { std::memset(data, 0, sizeof data); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t c) override {
        if (x < 0 || y < 0 || x >= 128 || y >= 64)
            return;
        unsigned char& p = data[y * 128 + x];
        if (c == C_INVERT)
            p ^= 1;
        else
            p = c == C_WHITE;
    }
};

class ReferenceContainer final : public RenderObject {
   public:
    RenderObject* child;
    int16_t width, height, padding, radius;
    bool border;
    ReferenceContainer(RenderObject* c, int16_t w, int16_t h, int16_t p, bool b, int16_t r)
        : child(c), width(w), height(h), padding(p), radius(r), border(b) {
        if (child)
            child->parent = this;
    }
    ~ReferenceContainer() override { delete child; }
    void performLayout() override {
        int16_t myW = width > 0 ? width : constraints.maxWidth;
        int16_t myH = height > 0 ? height : constraints.maxHeight;
        if (myW < constraints.minWidth)
            myW = constraints.minWidth;
        if (myW > constraints.maxWidth)
            myW = constraints.maxWidth;
        if (myH < constraints.minHeight)
            myH = constraints.minHeight;
        if (myH > constraints.maxHeight)
            myH = constraints.maxHeight;
        int16_t innerW = myW > padding * 2 ? myW - padding * 2 : 0;
        int16_t innerH = myH > padding * 2 ? myH - padding * 2 : 0;
        int16_t cw = 0, ch = 0;
        if (child) {
            child->layout(BoxConstraints::loose({innerW, innerH}));
            cw = child->size.width;
            ch = child->size.height;
        }
        if (width <= 0)
            myW = cw + padding * 2;
        if (height <= 0)
            myH = ch + padding * 2;
        size = {myW, myH};
        if (child) {
            child->pos = {
                static_cast<int16_t>(myW > cw + padding * 2 ? padding + (myW - padding * 2 - cw) / 2
                                                            : padding),
                static_cast<int16_t>(myH > ch + padding * 2 ? padding + (myH - padding * 2 - ch) / 2
                                                            : padding)};
        }
    }
    void paintWorker(Canvas* c, Point p) override {
        if (border) {
            if (radius > 0)
                c->drawRoundRect(p.x, p.y, size.width, size.height, radius, C_WHITE);
            else
                c->drawRect(p.x, p.y, size.width, size.height, C_WHITE);
        }
        if (child)
            child->paint(c, p);
    }
};

struct Case {
    const char* name;
    int16_t w, h, p, r;
};
static const Case cases[] = {{"fixed", 48, 24, 2, 4},     {"automatic", 0, 0, 2, 2},
                             {"auto-width", 0, 24, 2, 2}, {"auto-height", 64, 0, 2, 2},
                             {"icon", 28, 28, 0, 4},      {"card", 112, 24, 4, 4},
                             {"plain", 76, 22, 0, 0},     {"small", 20, 20, 2, 0}};

static void writeSheet(const unsigned char* sheet, const char* path) {
    const int scale = 4, w = 128 * scale, h = 64 * 8 * scale;
    unsigned char hdr[54]{};
    unsigned bytes = 54 + w * h * 3;
    hdr[0] = 'B';
    hdr[1] = 'M';
    hdr[10] = 54;
    hdr[14] = 40;
    hdr[26] = 1;
    hdr[28] = 24;
    for (int i = 0; i < 4; ++i) {
        hdr[2 + i] = (bytes >> (8 * i)) & 255;
        hdr[18 + i] = (w >> (8 * i)) & 255;
        hdr[22 + i] = (h >> (8 * i)) & 255;
    }
    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::perror(path);
        std::exit(2);
    }
    bool ok = std::fwrite(hdr, 1, 54, f) == 54;
    for (int y = h - 1; y >= 0; --y)
        for (int x = 0; x < w; ++x) {
            unsigned char v = sheet[(y / scale) * 128 + x / scale] ? 255 : 0;
            unsigned char rgb[] = {v, v, v};
            ok = (std::fwrite(rgb, 1, 3, f) == 3) && ok;
        }
    ok = std::fclose(f) == 0 && ok;
    if (!ok)
        std::exit(2);
}

int main() {
    Heap heap;
    NanoUI::MemoryPool::instance().setAdapter(&heap);
    Pixels oldPixels, newPixels;
    unsigned char oldSheet[128 * 64 * 8]{}, newSheet[128 * 64 * 8]{};
    int failures = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        const Case& c = cases[i];
        Text* oldText = new Text("NanoUI", 12);
        RenderObject* reference =
            new ReferenceContainer(oldText->createRenderObject(), c.w, c.h, c.p, true, c.r);
        Widget* widget = new Container(new Text("NanoUI", 12), c.w, c.h, c.p, true, c.r);
        RenderObject* actual = widget->createRenderObject();
        reference->layout(BoxConstraints::loose({128, 64}));
        actual->layout(BoxConstraints::loose({128, 64}));
        oldPixels.clear();
        newPixels.clear();
        oldPixels.resetClipRect();
        newPixels.resetClipRect();
        oldPixels.drawText(2, 2, c.name, 12);
        newPixels.drawText(2, 2, c.name, 12);
        reference->paint(&oldPixels, {3, 20});
        actual->paint(&newPixels, {3, 20});
        unsigned different = 0;
        for (unsigned j = 0; j < sizeof oldPixels.data; ++j)
            different += oldPixels.data[j] != newPixels.data[j];
        bool sameSize = reference->size.width == actual->size.width &&
                        reference->size.height == actual->size.height;
        if (different || !sameSize)
            ++failures;
        std::printf("%s: size=%dx%d pixels-different=%u\n", c.name, actual->size.width,
                    actual->size.height, different);
        std::memcpy(oldSheet + i * 128 * 64, oldPixels.data, sizeof oldPixels.data);
        std::memcpy(newSheet + i * 128 * 64, newPixels.data, sizeof newPixels.data);
        delete actual;
        delete widget;
        delete reference;
        delete oldText;
    }
    writeSheet(oldSheet, NANOUI_DYNAMIC_STORAGE ? "container-before-dynamic.bmp"
                                                : "container-before-pool.bmp");
    writeSheet(newSheet,
               NANOUI_DYNAMIC_STORAGE ? "container-after-dynamic.bmp" : "container-after-pool.bmp");
    if (heap.outstanding != 0 || NanoUI::MemoryPool::instance().getWidgetUsage() != 0 ||
        NanoUI::MemoryPool::instance().getRenderUsage() != 0)
        ++failures;
    NanoUI::MemoryPool::instance().setAdapter(nullptr);
    std::printf("container compatibility failures=%d\n", failures);
    return failures ? 1 : 0;
}
