#include <NanoUI.h>
#include <cstdio>
#include <cstring>

class FontCanvas : public Canvas {
public:
    uint8_t pixels[128 * 64] = {};
    void clear() override { std::memset(pixels, 0, sizeof pixels); }
    void display() override {}
    int16_t width() override { return 128; }
    int16_t height() override { return 64; }
    void drawPixel(int16_t x, int16_t y, uint16_t color) override {
        if (x < 0 || y < 0 || x >= 128 || y >= 64) return;
        uint8_t& pixel = pixels[y * 128 + x];
        if (color == C_INVERT) pixel ^= 1;
        else pixel = color == C_WHITE;
    }
};

int main() {
    struct Sample { const char* text; uint8_t size; uint8_t font; };
    const Sample samples[] = {
        {"字体演示", 12, 0}, {"恐龙快跑", 12, 0},
        {"像素对齐：你好世界", 12, 0}, {"高清像素：中文演示", 16, 0},
        {"平滑渲染：艺术字体", 16, 1}, {"0123456789", 24, 0},
        {"顶部底部甲乙", 10, 0}
    };
    int missing = 0;
    for (const auto& sample : samples) {
        const char* p = sample.text;
        while (*p) {
            const uint32_t u = decode_utf8(&p);
            uint8_t width = 0;
            const uint8_t* glyph = get_chinese_glyph(u, sample.size, &width, sample.font);
            bool ink = false;
            if (glyph) for (unsigned i = 0; i < ((width + 7) / 8) * sample.size; ++i) ink |= glyph[i] != 0;
            if (!glyph || !width || !ink) {
                ++missing;
                std::printf("Missing/empty U+%04x at %upx/id%u\n", unsigned(u), sample.size, sample.font);
            }
        }
    }
    if (missing) return 1;
    FontCanvas canvas;
    canvas.resetClipRect();
    canvas.drawText(0, 0, "字体演示 恐龙快跑", 12);
    canvas.drawText(0, 14, "像素对齐：你好世界", 12);
    canvas.drawText(0, 28, "中文演示", 16);
    canvas.drawText(0, 46, "艺术字体", 16, false, 1);
    // Scale the actual canvas pixels, not a system-font recreation.
    const unsigned width = 512, height = 256, bytes = 54 + width * height * 3;
    uint8_t header[54] = {};
    header[0] = 'B'; header[1] = 'M'; header[10] = 54; header[14] = 40;
    for (int i = 0; i < 4; ++i) {
        header[2+i] = (bytes >> (8*i)) & 255;
        header[18+i] = (width >> (8*i)) & 255;
        header[22+i] = (height >> (8*i)) & 255;
    }
    header[26] = 1; header[28] = 24;
    FILE* file = std::fopen("desktop-fonts.bmp", "wb");
    if (!file) return 2;
    bool ok = std::fwrite(header, 1, sizeof header, file) == sizeof header;
    for (int y = height - 1; y >= 0; --y) for (unsigned x = 0; x < width; ++x) {
        const uint8_t value = canvas.pixels[(y / 4) * 128 + x / 4] ? 255 : 0;
        const uint8_t rgb[] = {value, value, value};
        ok = (std::fwrite(rgb, 1, 3, file) == 3) && ok;
    }
    ok = std::fclose(file) == 0 && ok;
    std::puts("Desktop glyph coverage and rasterization PASS");
    return ok ? 0 : 3;
}
