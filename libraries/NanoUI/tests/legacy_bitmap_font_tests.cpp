#include <NanoUI/ports/legacy_bitmap_font.h>
#include <windows/raster_canvas.h>

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
int failures = 0;
#define CHECK(expr)                                              \
    do {                                                         \
        ++checks;                                                \
        if (!(expr)) {                                           \
            ++failures;                                          \
            std::cerr << "FAIL " << __LINE__ << ": " #expr "\n"; \
        }                                                        \
    } while (0)

std::uint8_t ascii[256 * 5] = {};
std::uint8_t seenFont = 0;
std::uint8_t seenHeight = 0;
unsigned lookupCalls = 0;
const std::uint8_t glyph[] = {0x80, 0x80, 0x40};  // Row byte padding matches Canvas::drawBitmap.

const std::uint8_t* lookup(std::uint32_t cp, std::uint8_t height, std::uint8_t* width,
                           std::uint8_t fontId) {
    ++lookupCalls;
    seenFont = fontId;
    seenHeight = height;
    if (cp == 0x2603) {
        *width = 2;
        return glyph;
    }
    return nullptr;
}

void setup() {
    ascii[static_cast<unsigned>('A') * 5] = 0x81;
    ascii[static_cast<unsigned>('A') * 5 + 1] = 0x02;
    ascii[static_cast<unsigned>('B') * 5 + 4] = 0x80;
    seenFont = seenHeight = 0;
    lookupCalls = 0;
}

void testValidGlyphAndSpacing() {
    setup();
    NanoUI::LegacyBitmapFont font(ascii, lookup, 7);
    const char snow[] = "\xE2\x98\x83";
    const NanoUI::Size measured = font.measure(NanoUI::TextView(snow, 3), 3);
    CHECK(measured.width == 2 &&
          measured.height == 3);  // Resource advance is its width, no invented gap.
    CHECK(seenFont == 7 && seenHeight == 3 && lookupCalls != 0);
    NanoUI::RasterCanvas canvas(5, 4);
    font.paint(canvas, NanoUI::Offset(1, 1), NanoUI::TextView(snow, 3), 3, 0x12345678u);
    CHECK(canvas.pixel(1, 1) == 0x12345678u);
    CHECK(canvas.pixel(1, 2) == 0x12345678u);
    CHECK(canvas.pixel(2, 3) == 0x12345678u);
    CHECK(canvas.pixel(2, 1) == 0 && canvas.pixel(1, 3) == 0);
    CHECK(canvas.balanced() && !canvas.error());
}

void testAsciiFallbackAndIntegerScaling() {
    setup();
    NanoUI::LegacyBitmapFont font(ascii, nullptr);
    CHECK(font.measure(NanoUI::TextView("A", 1), 1).width == 6);
    CHECK(font.measure(NanoUI::TextView("AB", 2), 2).width == 12);
    CHECK(font.measure(NanoUI::TextView("A", 1), 16).width == 12);
    NanoUI::RasterCanvas canvas(30, 20);
    font.paint(canvas, NanoUI::Offset(0, 0), NanoUI::TextView("A", 1), 16, 0xAABBCCDDu);
    CHECK(canvas.pixel(0, 0) == 0xAABBCCDDu && canvas.pixel(1, 1) == 0xAABBCCDDu);
    CHECK(canvas.pixel(0, 2) == 0 && canvas.pixel(0, 14) == 0xAABBCCDDu);
    CHECK(canvas.pixel(2, 2) == 0xAABBCCDDu && canvas.pixel(4, 2) == 0);
    CHECK(canvas.pixel(12, 0) == 0);  // one 12px advance including the trailing gap
    CHECK(!canvas.error());
}

void testMissingNonAsciiBoxAndAdvance() {
    setup();
    NanoUI::LegacyBitmapFont font(ascii, nullptr);
    const char missing[] = "\xC2\xA2";
    CHECK(font.measure(NanoUI::TextView(missing, 2), 4).width == 4);
    NanoUI::RasterCanvas canvas(6, 6);
    font.paint(canvas, NanoUI::Offset(), NanoUI::TextView(missing, 2), 4, 0x01020304u);
    CHECK(canvas.pixel(0, 0) == 0x01020304u && canvas.pixel(3, 0) == 0x01020304u);
    CHECK(canvas.pixel(0, 3) == 0x01020304u && canvas.pixel(3, 3) == 0x01020304u);
    CHECK(canvas.pixel(1, 1) == 0 && !canvas.error());
}

void testBoundedMalformedUtf8() {
    setup();
    NanoUI::LegacyBitmapFont font(ascii, nullptr);
    const char truncated[] = {static_cast<char>(0xE2), static_cast<char>(0x82)};
    const char nonNul[] = {'A', 'B', 'X'};
    const char malformed[] = {static_cast<char>(0xE2), 'A'};
    const char overlong[] = {static_cast<char>(0xC0), static_cast<char>(0xAF)};
    const char surrogate[] = {static_cast<char>(0xED), static_cast<char>(0xA0),
                              static_cast<char>(0x80)};
    const char tooLarge[] = {static_cast<char>(0xF4), static_cast<char>(0x90),
                             static_cast<char>(0x80), static_cast<char>(0x80)};
    CHECK(font.measure(NanoUI::TextView(truncated, sizeof truncated), 1).width == 2);
    CHECK(font.measure(NanoUI::TextView(nonNul, 2), 1).width == 12);
    CHECK(font.measure(NanoUI::TextView(malformed, sizeof malformed), 1).width == 7);
    CHECK(font.measure(NanoUI::TextView(overlong, sizeof overlong), 1).width == 2);
    CHECK(font.measure(NanoUI::TextView(surrogate, sizeof surrogate), 1).width == 3);
    CHECK(font.measure(NanoUI::TextView(tooLarge, sizeof tooLarge), 1).width == 4);
    CHECK(font.measure(NanoUI::TextView(nullptr, 1), 1).width == 0);
    NanoUI::RasterCanvas canvas(24, 3);
    font.paint(canvas, NanoUI::Offset(), NanoUI::TextView(nonNul, 2), 1, 0xFFFFFFFFu);
    CHECK(canvas.pixel(0, 0) == 0xFFFFFFFFu && canvas.pixel(6, 0) == 0);
}

void testInvalidHeightsAreEmptyAndNoop() {
    setup();
    NanoUI::LegacyBitmapFont font(ascii, lookup);
    const float invalid[] = {0.0f,
                             -1.0f,
                             1.5f,
                             256.0f,
                             std::numeric_limits<float>::quiet_NaN(),
                             std::numeric_limits<float>::infinity(),
                             -std::numeric_limits<float>::infinity()};
    NanoUI::RasterCanvas canvas(8, 8);
    for (unsigned i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        const NanoUI::Size measured = font.measure(NanoUI::TextView("A", 1), invalid[i]);
        CHECK(measured.width == 0 && measured.height == 0);
        font.paint(canvas, NanoUI::Offset(), NanoUI::TextView("A", 1), invalid[i], 0xFFFFFFFFu);
    }
    font.paint(canvas, NanoUI::Offset(std::numeric_limits<float>::infinity(), 0),
               NanoUI::TextView("A", 1), 1, 0xFFFFFFFFu);
    font.paint(canvas, NanoUI::Offset(), NanoUI::TextView(nullptr, 1), 1, 0xFFFFFFFFu);
    CHECK(canvas.pixel(0, 0) == 0 && !canvas.error());
}
void testNullResourcesHaveConsistentAdvance() {
    NanoUI::LegacyBitmapFont font(nullptr, nullptr);
    CHECK(font.measure("AA", 4).width == 8);
    NanoUI::RasterCanvas canvas(9, 4);
    font.paint(canvas, {}, "AA", 4, 0xFFFFFFFFu);
    CHECK(canvas.pixel(0, 0) == 0xFFFFFFFFu && canvas.pixel(4, 0) == 0xFFFFFFFFu);
    CHECK(canvas.pixel(1, 1) == 0 && canvas.pixel(5, 1) == 0 && canvas.pixel(8, 0) == 0);
}
}  // namespace

int main() {
    testValidGlyphAndSpacing();
    testAsciiFallbackAndIntegerScaling();
    testMissingNonAsciiBoxAndAdvance();
    testBoundedMalformedUtf8();
    testInvalidHeightsAreEmptyAndNoop();
    testNullResourcesHaveConsistentAdvance();
    std::cout << "legacy bitmap font: " << (checks - failures) << '/' << checks
              << " checks passed\n";
    return failures ? 1 : 0;
}
