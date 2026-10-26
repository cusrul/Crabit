#pragma once
#include "paint_canvas.h"
#include <NanoUI/foundation/text_view.h>

namespace NanoUI {
// Platform font resources can implement this without changing Text or layouts.
// Resource lifetime exceeds every Widget/RenderObject that refers to the font.
class Font {
public:
    virtual ~Font() noexcept = default;
    virtual Size measure(TextView, float pixelHeight) const noexcept = 0;
    virtual void paint(PaintCanvas&, Offset, TextView, float pixelHeight,
                       std::uint32_t color) const noexcept = 0;
};
class BitmapFont5x7 final : public Font {
public:
    BitmapFont5x7(const std::uint8_t* glyphs, unsigned first, unsigned last) noexcept
        : glyphs_(glyphs), first_(first), last_(last) {}
    Size measure(TextView text, float height) const noexcept override {
        if (!valid(height) || (text.size() && !text.data())) return {};
        std::size_t count = 0;
        for (std::size_t index = 0; index < text.size(); ++count) advance(text, index);
        return Size(count ? (static_cast<float>(count) * 6 - 1) * height / 7 : 0, height);
    }
    void paint(PaintCanvas& canvas, Offset origin, TextView text, float height,
               std::uint32_t color) const noexcept override {
        if (!valid(height) || !origin.isFinite() || (text.size() && !text.data())) return;
        const float scale = height / 7;
        for (std::size_t index = 0; index < text.size();) {
            const unsigned glyph = advance(text, index);
            const unsigned code = glyph >= first_ && glyph <= last_ ? glyph : static_cast<unsigned>('?');
            if (code < first_ || code > last_) return;
            for (unsigned x = 0; x < 5; ++x) {
                const auto bits = glyphs_[(code - first_) * 5 + x];
                for (unsigned y = 0; y < 7; ++y)
                    if (bits & (1u << y))
                        canvas.fillRect(Rect(origin.dx + x * scale, origin.dy + y * scale,
                            origin.dx + (x + 1) * scale, origin.dy + (y + 1) * scale), color);
            }
            origin.dx += 6 * scale;
        }
    }
private:
    bool valid(float height) const noexcept {
        return glyphs_ && first_ <= last_ && std::isfinite(height) && height > 0;
    }
    static unsigned advance(TextView text, std::size_t& index) noexcept {
        const unsigned lead = static_cast<unsigned char>(text.data()[index++]);
        if (lead < 128) return lead;
        const unsigned remaining = lead >= 0xC2 && lead <= 0xDF ? 1 :
            lead >= 0xE0 && lead <= 0xEF ? 2 : lead >= 0xF0 && lead <= 0xF4 ? 3 : 0;
        for (unsigned n = 0; n < remaining && index < text.size(); ++n) {
            if ((static_cast<unsigned char>(text.data()[index]) & 0xC0) != 0x80) break;
            ++index;
        }
        return '?'; // ASCII diagnostic font, not a CJK shaping implementation.
    }
    const std::uint8_t* glyphs_;
    unsigned first_, last_;
};
} // namespace NanoUI
