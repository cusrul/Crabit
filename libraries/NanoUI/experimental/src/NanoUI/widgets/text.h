#pragma once
#include "layout.h"
#include <NanoUI/foundation/owned_text.h>
#include <NanoUI/rendering/font.h>
#include <cstring>

namespace NanoUI {
enum class TextOverflow { clip, ellipsis };
class RenderText final : public RenderObject {
public:
    explicit RenderText(Allocator& allocator) noexcept : text_(allocator) {}
    UiError configure(TextView value, const Font& font, float height, std::uint32_t color,
                      TextOverflow overflow = TextOverflow::clip) noexcept {
        if (!std::isfinite(height) || height <= 0 || (value.size() && !value.data()))
            return UiError::invalidInput;
        if (value.size() != text_.size() ||
            (value.size() && std::memcmp(value.data(), text_.c_str(), value.size()) != 0)) {
            const auto copied = text_.assign(value);
            if (copied != TextResult::ok)
                return copied == TextResult::outOfMemory ? UiError::outOfMemory : UiError::invalidInput;
        }
        font_ = &font; height_ = height; color_ = color; overflow_ = overflow; markNeedsLayout();
        return UiError::none;
    }
protected:
    Size performLayout(const BoxConstraints&) noexcept override {
        return font_ ? font_->measure(TextView(text_.c_str(), text_.size()), height_) : Size{};
    }
    void performPaint(PaintCanvas& canvas) noexcept override {
        if (!font_) return;
        const TextView text(text_.c_str(), text_.size());
        if (overflow_ == TextOverflow::clip || font_->measure(text, height_).width <= size().width) {
            font_->paint(canvas, {}, text, height_, color_); return;
        }
        const float suffix = font_->measure("...", height_).width;
        if (suffix > size().width) return;
        std::size_t end = 0;
        float width = 0;
        while (end < text.size()) {
            std::size_t next = end + 1;
            while (next < text.size() && (static_cast<unsigned char>(text.data()[next]) & 0xC0) == 0x80) ++next;
            const float measured = font_->measure(TextView(text.data(), next), height_).width;
            if (measured + height_ / 7 + suffix > size().width) break;
            end = next; width = measured;
        }
        font_->paint(canvas, {}, TextView(text.data(), end), height_, color_);
        font_->paint(canvas, Offset(end ? width + height_ / 7 : 0, 0), "...", height_, color_);
    }
private:
    OwnedText text_;
    const Font* font_ = nullptr;
    float height_ = 7;
    std::uint32_t color_ = 0xFFFFFFFFu;
    TextOverflow overflow_ = TextOverflow::clip;
};
class Text final : public RenderWidget {
public:
    Text(OwnedText&& text, const Font& font, float height, std::uint32_t color, Key key,
         TextOverflow overflow = TextOverflow::clip) noexcept
        : RenderWidget(typeId<Text>(), key), text_(std::move(text)), font_(&font), height_(height), color_(color), overflow_(overflow) {}
    Owned<RenderObject> createRenderObject(Allocator& a) const noexcept override {
        return makeOwned<RenderText>(a, AllocationKind::renderObject, a);
    }
    UiError updateRenderObject(RenderObject& r) const noexcept override {
        return static_cast<RenderText&>(r).configure(TextView(text_.c_str(), text_.size()), *font_, height_, color_, overflow_);
    }
private:
    OwnedText text_;
    const Font* font_;
    float height_;
    std::uint32_t color_;
    TextOverflow overflow_;
};
inline WidgetPtr makeText(Allocator& a, TextView value, const Font& font, float height = 7,
                          std::uint32_t color = 0xFFFFFFFFu, Key key = {},
                          TextOverflow overflow = TextOverflow::clip) noexcept {
    OwnedText owned(a);
    if (owned.assign(value) != TextResult::ok) return {};
    return makeOwned<Text>(a, AllocationKind::widget, std::move(owned), font, height, color, key, overflow);
}
} // namespace NanoUI
