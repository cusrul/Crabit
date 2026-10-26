#pragma once

#include <NanoUI/rendering/font.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace NanoUI {

// Non-owning bridge for the legacy row-major bitmap resources. The ASCII table
// and any storage returned by lookup must outlive every user of this font.
class LegacyBitmapFont final : public Font {
public:
    using GlyphLookup = const std::uint8_t* (*)(std::uint32_t, std::uint8_t,
                                                std::uint8_t*, std::uint8_t);

    LegacyBitmapFont(const std::uint8_t* ascii5x8, GlyphLookup lookup,
                     std::uint8_t fontId = 0) noexcept
        : ascii5x8_(ascii5x8), lookup_(lookup), fontId_(fontId) {}

    Size measure(TextView text, float pixelHeight) const noexcept override {
        if (!validHeight(pixelHeight) || (text.size() && !text.data())) return {};
        const unsigned height = static_cast<unsigned>(pixelHeight);
        std::size_t offset = 0;
        std::uint64_t advance = 0;
        while (offset < text.size()) {
            const std::uint32_t codepoint = decode(text, offset);
            advance += glyphAdvance(codepoint, height);
        }
        // The legacy contract includes the spacing gap after the final glyph.
        return Size(static_cast<float>(advance), pixelHeight);
    }

    void paint(PaintCanvas& canvas, Offset origin, TextView text, float pixelHeight,
               std::uint32_t color) const noexcept override {
        if (!validHeight(pixelHeight) || !origin.isFinite() ||
            (text.size() && !text.data())) return;
        const unsigned height = static_cast<unsigned>(pixelHeight);
        for (std::size_t offset = 0; offset < text.size();) {
            const std::uint32_t codepoint = decode(text, offset);
            std::uint8_t width = 0;
            const std::uint8_t* bitmap = lookup_ ? lookup_(codepoint, static_cast<std::uint8_t>(height),
                                                          &width, fontId_) : nullptr;
            unsigned advance = height;
            if (bitmap) {
                advance = width;
                for (unsigned y = 0; y < height; ++y) {
                    for (unsigned x = 0; x < width; ++x) {
                        const std::size_t byte = static_cast<std::size_t>(y) * ((width + 7u) / 8u) + x / 8u;
                        if (bitmap[byte] & static_cast<std::uint8_t>(0x80u >> (x % 8u)))
                            canvas.fillRect(Rect(origin.dx + static_cast<float>(x),
                                origin.dy + static_cast<float>(y), origin.dx + x + 1.0f,
                                origin.dy + y + 1.0f), color);
                    }
                }
            } else if (codepoint < 128 && ascii5x8_) {
                const unsigned scale = height >= 16 ? height / 8 : 1;
                advance = 6 * scale;
                for (unsigned x = 0; x < 5; ++x) {
                    const std::uint8_t column = ascii5x8_[codepoint * 5u + x];
                    for (unsigned y = 0; y < 8; ++y) {
                        if (column & static_cast<std::uint8_t>(1u << y))
                            canvas.fillRect(Rect(origin.dx + static_cast<float>(x * scale),
                                origin.dy + static_cast<float>(y * scale),
                                origin.dx + static_cast<float>((x + 1) * scale),
                                origin.dy + static_cast<float>((y + 1) * scale)), color);
                    }
                }
            } else {
                // Missing non-ASCII resources are represented by a 1px outline.
                const unsigned edge = height > 1 ? height - 1 : 1;
                canvas.fillRect(Rect(origin.dx, origin.dy, origin.dx + height, origin.dy + 1), color);
                if (height > 1) {
                    canvas.fillRect(Rect(origin.dx, origin.dy + edge, origin.dx + height,
                                         origin.dy + height), color);
                    canvas.fillRect(Rect(origin.dx, origin.dy + 1, origin.dx + 1,
                                         origin.dy + edge), color);
                    canvas.fillRect(Rect(origin.dx + edge, origin.dy + 1, origin.dx + height,
                                         origin.dy + edge), color);
                }
            }
            origin.dx += static_cast<float>(advance);
        }
    }

private:
    static bool validHeight(float height) noexcept {
        return std::isfinite(height) && height >= 1.0f && height <= 255.0f &&
               std::floor(height) == height;
    }

    static std::uint32_t decode(TextView text, std::size_t& offset) noexcept {
        const std::size_t start = offset;
        const std::uint8_t lead = static_cast<std::uint8_t>(text.data()[offset++]);
        if (lead < 0x80) return lead;
        unsigned trailing;
        std::uint32_t value;
        std::uint32_t minimum;
        if (lead >= 0xC2 && lead <= 0xDF) { trailing = 1; value = lead & 0x1Fu; minimum = 0x80; }
        else if (lead >= 0xE0 && lead <= 0xEF) { trailing = 2; value = lead & 0x0Fu; minimum = 0x800; }
        else if (lead >= 0xF0 && lead <= 0xF4) { trailing = 3; value = lead & 0x07u; minimum = 0x10000; }
        else return 0xFFFD;
        if (text.size() - start <= trailing) { offset = start + 1; return 0xFFFD; }
        for (unsigned i = 0; i < trailing; ++i) {
            const std::uint8_t byte = static_cast<std::uint8_t>(text.data()[start + 1 + i]);
            if ((byte & 0xC0u) != 0x80u) { offset = start + 1; return 0xFFFD; }
            value = (value << 6) | (byte & 0x3Fu);
        }
        offset = start + trailing + 1;
        if (value < minimum || (value >= 0xD800 && value <= 0xDFFF) || value > 0x10FFFF) {
            offset = start + 1;
            return 0xFFFD;
        }
        return value;
    }

    unsigned glyphAdvance(std::uint32_t codepoint, unsigned height) const noexcept {
        if (lookup_) {
            std::uint8_t width = 0;
            if (lookup_(codepoint, static_cast<std::uint8_t>(height), &width, fontId_))
                return static_cast<unsigned>(width);
        }
        if (codepoint < 128 && ascii5x8_) return 6u * (height >= 16 ? height / 8 : 1u);
        return height;
    }

    const std::uint8_t* ascii5x8_;
    GlyphLookup lookup_;
    std::uint8_t fontId_;
};

} // namespace NanoUI
