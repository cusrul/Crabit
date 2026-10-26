#include "../ports/windows/raster_canvas.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
int checks = 0, failures = 0;
#define CHECK(e)                                   \
    do {                                           \
        ++checks;                                  \
        if (!(e)) {                                \
            ++failures;                            \
            std::cerr << __LINE__ << ": " #e "\n"; \
        }                                          \
    } while (false)
using namespace NanoUI;

void nestedTranslationAndClip() {
    RasterCanvas c(6, 5);
    const std::uint32_t red = 0xff0000ff, green = 0x00ff00ff, blue = 0x0000ffff;
    c.save();
    c.translate(Offset(1, 1));
    c.clipRect(Rect(0, 0, 4, 3));
    c.save();
    c.translate(Offset(2, 1));
    c.fillRect(Rect(-1, -1, 4, 4), red);
    c.restore();
    c.fillRect(Rect(0, 0, 1, 1), green);
    c.restore();
    CHECK(c.balanced() && !c.error());
    CHECK(c.pixel(2, 1) == red && c.pixel(4, 3) == red);
    CHECK(c.pixel(5, 3) == 0 && c.pixel(1, 1) == green);
    c.clear(blue);
    CHECK(c.pixel(0, 0) == blue && c.pixel(5, 4) == blue);
    c.clear(0);
    CHECK(c.pixel(2, 1) == 0);
}

void pixelCentersAndInvalidGeometry() {
    RasterCanvas c(4, 3);
    c.fillRect(Rect(0.5f, 0.5f, 1.5f, 1.5f), 1);
    CHECK(c.pixel(0, 0) == 1 && c.pixel(1, 0) == 0);
    CHECK(c.pixel(0, 1) == 0 && c.pixel(1, 1) == 0);
    c.fillRect(Rect(-1.0e30f, -1.0e30f, -1.0e29f, -1.0e29f), 2);
    c.fillRect(Rect(1.0e29f, 1.0e29f, 1.0e30f, 1.0e30f), 2);
    CHECK(!c.error() && c.pixel(0, 0) == 1);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    RasterCanvas bad(2, 2);
    bad.fillRect(Rect(0, 0, nan, 1), 9);
    bad.fillRect(Rect(0, 0, 2, 2), 8);
    CHECK(bad.error() && bad.pixel(0, 0) == 0);
    RasterCanvas badTranslation(2, 2);
    badTranslation.translate(Offset(inf, 0));
    badTranslation.fillRect(Rect(0, 0, 2, 2), 3);
    CHECK(badTranslation.error() && badTranslation.pixel(0, 0) == 0);
}

void stackErrorsAndPpm() {
    RasterCanvas overflow(1, 1);
    for (std::size_t i = 0; i < RenderObject::maxDepth; ++i)
        overflow.save();
    CHECK(!overflow.balanced() && !overflow.error());
    overflow.save();
    overflow.fillRect(Rect(0, 0, 1, 1), 4);
    CHECK(overflow.error() && overflow.pixel(0, 0) == 0);
    RasterCanvas underflow(1, 1);
    underflow.restore();
    underflow.fillRect(Rect(0, 0, 1, 1), 4);
    CHECK(underflow.error() && underflow.balanced() && underflow.pixel(0, 0) == 0);

    RasterCanvas ppm(2, 1);
    ppm.clear(0x123456ff);
    ppm.fillRect(Rect(1, 0, 2, 1), 0xabcdef00);
    const std::string path = "nanoui-raster-canvas-test.ppm";
    CHECK(ppm.writePpm(path.c_str()));
    std::ifstream in(path, std::ios::binary);
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), {});
    CHECK(bytes.size() == 17);
    const std::string header(bytes.begin(),
                             bytes.begin() + (bytes.size() >= 11 ? 11 : bytes.size()));
    CHECK(header == "P6\n2 1\n255\n");
    CHECK(bytes.size() >= 17 && bytes[11] == 0x12 && bytes[12] == 0x34 && bytes[13] == 0x56 &&
          bytes[14] == 0xab && bytes[15] == 0xcd && bytes[16] == 0xef);
    in.close();
    CHECK(std::remove(path.c_str()) == 0);
    CHECK(!ppm.writePpm(nullptr));
}

void invalidCanvasDimensions() {
    const std::size_t max = std::numeric_limits<std::size_t>::max();
    RasterCanvas wrapZero(max / 2 + 1, 2);
    CHECK(wrapZero.error() && wrapZero.pixel(0, 0) == 0);
    RasterCanvas wrapNonzero(max, 2);
    CHECK(wrapNonzero.error() && wrapNonzero.pixel(0, 0) == 0);
    RasterCanvas zeroWidth(0, 2);
    RasterCanvas zeroHeight(2, 0);
    CHECK(zeroWidth.error() && zeroWidth.pixel(0, 0) == 0);
    CHECK(zeroHeight.error() && zeroHeight.pixel(0, 0) == 0);
    RasterCanvas impossible(max, 1);
    CHECK(impossible.error() && impossible.pixel(0, 0) == 0);
    RasterCanvas overCapacity(std::vector<std::uint32_t>().max_size() + 1, 1);
    CHECK(overCapacity.error() && overCapacity.pixel(0, 0) == 0);
}
}  // namespace

int main() {
    nestedTranslationAndClip();
    pixelCentersAndInvalidGeometry();
    stackErrorsAndPpm();
    invalidCanvasDimensions();
    std::cout << "raster canvas: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
