#include <NanoUI/ports/pixel_canvas.h>
#include <NanoUI/ports/legacy_canvas.h>
#include <windows/raster_canvas.h>
#include <cstdio>
#include <limits>
namespace {
using namespace NanoUI;
int checks = 0, failures = 0;
#define CHECK(v)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(v)) {                                \
            ++failures;                            \
            std::printf("%d: %s\n", __LINE__, #v); \
        }                                          \
    } while (false)
struct Pixels {
    std::uint32_t values[64]{};
    int writes = 0;
    static void put(int x, int y, std::uint32_t c, void* p) noexcept {
        auto& pixels = *static_cast<Pixels*>(p);
        CHECK(x >= 0 && x < 8 && y >= 0 && y < 8);
        if (x >= 0 && x < 8 && y >= 0 && y < 8)
            pixels.values[y * 8 + x] = c;
        ++pixels.writes;
    }
};
void parity() {
    Pixels pixels;
    PixelCanvas port(8, 8, PixelSink(Pixels::put, &pixels));
    RasterCanvas reference(8, 8);
    unsigned seed = 12345;
    const auto value = [&]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int>(seed % 96) - 32) / 4;
    };
    for (int n = 0; n < 1000; ++n) {
        port.save();
        reference.save();
        const Offset offset(value(), value());
        port.translate(offset);
        reference.translate(offset);
        const Rect clip(value(), value(), value(), value());
        port.clipRect(clip);
        reference.clipRect(clip);
        const Rect rect(value(), value(), value(), value());
        port.fillRect(rect, seed);
        reference.fillRect(rect, seed);
        port.restore();
        reference.restore();
        for (int i = 0; i < 64; ++i)
            CHECK(pixels.values[i] == reference.pixel(i % 8, i / 8));
    }
    CHECK(port.balanced() && !port.error());
    port.fillRect(Rect(-1e30f, -1e30f, 1e30f, 1e30f), 7);
    CHECK(pixels.writes > 0);
    for (auto c : pixels.values)
        CHECK(c == 7);
}
struct Legacy {
    Rect clipping{1, 1, 7, 7};
    std::uint16_t pixels[64]{};
    int clears = 0, displays = 0;
    int width() const { return 8; }
    int height() const { return 8; }
    Rect getClipRect() const { return clipping; }
    void clear() { ++clears; }
    void display() { ++displays; }
    void drawPixel(std::int16_t x, std::int16_t y, std::uint16_t c) {
        CHECK(x >= 0 && x < 8 && y >= 0 && y < 8);
        CHECK(c <= 1);
        pixels[y * 8 + x] = c;
    }
};
void legacyBoundary() {
    Legacy legacy;
    LegacyCanvasBridge<Legacy> bridge(legacy);
    bridge.fillRect(Rect(0, 0, 8, 8), 0xFFFFFFFFu);
    CHECK(legacy.pixels[0] == 0 && legacy.pixels[9] == 1 && legacy.pixels[63] == 0);
    CHECK(legacy.clipping.left == 1 && legacy.clipping.right == 7);
    CHECK(legacy.clears == 0 && legacy.displays == 0);
    legacy.clipping = Rect(3, 3, 5, 5);
    CHECK(bridge.beginFrame());
    bridge.fillRect(Rect(0, 0, 8, 8), 0x000000FFu);
    CHECK(legacy.pixels[27] == 0 && legacy.pixels[9] == 1);
    bridge.fillRect(Rect(3, 3, 5, 5), 0x80808000u);
    CHECK(legacy.pixels[27] == 1);  // Opaque mapping, alpha ignored like RasterCanvas.
    bridge.fillRect(Rect(3, 3, 5, 5), 0x7F7F7FFFu);
    CHECK(legacy.pixels[27] == 0);
    CHECK(legacy.clipping.left == 3 && legacy.clipping.right == 5);
}
void errors() {
    Pixels pixels;
    PixelCanvas bad(-1, 8, PixelSink(Pixels::put, &pixels));
    CHECK(bad.error());
    PixelCanvas huge(32768, 8, PixelSink(Pixels::put, &pixels));
    CHECK(huge.error());
    PixelCanvas empty(8, 8, {});
    CHECK(empty.error());
    PixelCanvas overflow(8, 8, PixelSink(Pixels::put, &pixels));
    for (unsigned i = 0; i < RenderObject::maxDepth; ++i)
        overflow.save();
    CHECK(!overflow.error());
    overflow.save();
    CHECK(overflow.error());
    overflow.fillRect(Rect(0, 0, 8, 8), 7);
    CHECK(pixels.writes == 0);
    PixelCanvas underflow(8, 8, PixelSink(Pixels::put, &pixels));
    underflow.restore();
    CHECK(underflow.error());
    PixelCanvas invalid(8, 8, PixelSink(Pixels::put, &pixels));
    invalid.translate(Offset(std::numeric_limits<float>::infinity(), 0));
    CHECK(invalid.error());
    invalid.fillRect(Rect(0, 0, 8, 8), 7);
    CHECK(pixels.writes == 0);
    PixelCanvas unbalanced(8, 8, PixelSink(Pixels::put, &pixels));
    unbalanced.save();
    CHECK(!unbalanced.beginFrame(Rect(0, 0, 8, 8)) && unbalanced.error());
}
}  // namespace
int main() {
    parity();
    legacyBoundary();
    errors();
    std::printf("pixel port: %d checks, %d failures; sizeof(PixelCanvas)=%u\n", checks, failures,
                static_cast<unsigned>(sizeof(NanoUI::PixelCanvas)));
    return failures ? 1 : 0;
}
