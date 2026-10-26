// Host validation only: exercise actual font provider and media renderer.
#include <NanoUI.h>
#include <cstdio>
#include <cstring>

namespace {
uint32_t now = 0;
uint32_t ticks() { return now; }
void delayMs(uint32_t) {}
class Pixels : public Canvas {
public:
    uint8_t data[128 * 64] = {};
    void clear() override { std::memset(data, 0, sizeof data); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= 128 || y >= 64) return;
        uint8_t& pixel = data[y * 128 + x];
        if (color == C_INVERT) pixel ^= 1;
        else pixel = color == C_WHITE;
    }
};
bool save(const Pixels& canvas, const char* filename) {
    unsigned char header[54] = {};
    const unsigned bytes = 54 + 128 * 64 * 3;
    header[0] = 'B'; header[1] = 'M';
    for (int i = 0; i < 4; ++i) header[2+i] = (bytes >> (8*i)) & 255;
    header[10] = 54; header[14] = 40; header[18] = 128; header[22] = 64;
    header[26] = 1; header[28] = 24;
    FILE* file = std::fopen(filename, "wb");
    if (!file) return false;
    bool ok = std::fwrite(header, 1, 54, file) == 54;
    for (int y = 63; y >= 0; --y) for (int x = 0; x < 128; ++x) {
        const uint8_t color = canvas.data[y*128+x] ? 255 : 0;
        const uint8_t rgb[] = {color, color, color};
        ok = (std::fwrite(rgb, 1, 3, file) == 3) && ok;
    }
    return std::fclose(file) == 0 && ok;
}
}
int main() {
    HAL_Time::init(ticks, delayMs);
    Pixels canvas;
    canvas.resetClipRect();
    uint8_t width = 0;
    if (!get_chinese_glyph(0x793a, 12, &width, 0) || !width) return 1;
    canvas.drawText(0, 0, "NanoUI 示例", 12, false, 0);
    // Same 12-byte header/page bytes verified by the Python exporter test.
    alignas(AssetHeader) const uint8_t animation[] = {
        0x55, 0x4e, 1, 2, 2, 0, 9, 0, 2, 0, 10, 0,
        1, 128, 1, 0, 2, 64, 0, 1
    };
    RenderAnimatedImage image(animation, true);
    image.layout(BoxConstraints::loose({128, 64}));
    image.paint(&canvas, {0, 20});
    if (!canvas.data[20*128] || !canvas.data[28*128]) return 2;
    if (!save(canvas, "resources-frame0.bmp")) return 3;
    canvas.clear();
    canvas.drawText(0, 0, "NanoUI 示例", 12, false, 0);
    now = 100;
    image.paint(&canvas, {0, 20});
    if (canvas.data[20*128] || !canvas.data[21*128] || !canvas.data[28*128+1]) return 4;
    if (!save(canvas, "resources-frame1.bmp")) return 5;
    std::puts("Chinese glyph and two timed media frames PASS");
    return 0;
}
