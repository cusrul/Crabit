#pragma once

#include <cmath>
#include <limits>

namespace NanoUI {

struct Offset {
    float dx, dy;
    Offset(float x = 0, float y = 0) noexcept : dx(x), dy(y) {}
    bool isFinite() const noexcept { return std::isfinite(dx) && std::isfinite(dy); }
    Offset operator+(Offset b) const noexcept { return {dx + b.dx, dy + b.dy}; }
    Offset operator-(Offset b) const noexcept { return {dx - b.dx, dy - b.dy}; }
};

struct Size {
    float width, height;
    Size(float w = 0, float h = 0) noexcept : width(w), height(h) {}
};

// Half-open bounds: adjacent boxes never both own an edge hit.
struct Rect {
    float left, top, right, bottom;
    Rect(float l = 0, float t = 0, float r = 0, float b = 0) noexcept
        : left(l), top(t), right(r), bottom(b) {}
    static Rect fromSize(Size s) noexcept { return {0, 0, s.width, s.height}; }
    bool contains(Offset p) const noexcept {
        return p.isFinite() && p.dx >= left && p.dx < right && p.dy >= top && p.dy < bottom;
    }
    Rect intersect(Rect b) const noexcept {
        const float l = left > b.left ? left : b.left;
        const float t = top > b.top ? top : b.top;
        const float r = right < b.right ? right : b.right;
        const float d = bottom < b.bottom ? bottom : b.bottom;
        return {l, t, r < l ? l : r, d < t ? t : d};
    }
};

struct BoxConstraints {
    float minWidth, maxWidth, minHeight, maxHeight;
    BoxConstraints() noexcept
        : minWidth(0), maxWidth(std::numeric_limits<float>::infinity()),
          minHeight(0), maxHeight(std::numeric_limits<float>::infinity()) {}
    BoxConstraints(float minW, float maxW, float minH, float maxH) noexcept
        : minWidth(minW), maxWidth(maxW), minHeight(minH), maxHeight(maxH) {}
    static BoxConstraints tight(Size s) noexcept { return {s.width, s.width, s.height, s.height}; }
    bool isNormalized() const noexcept {
        return std::isfinite(minWidth) && std::isfinite(minHeight) &&
            minWidth >= 0 && minHeight >= 0 && maxWidth >= minWidth && maxHeight >= minHeight;
    }
    // Negative finite values clamp to zero; reversed maxima rise to minima.
    // NaN, infinite minima and negative-infinite maxima are rejected atomically.
    bool normalize(BoxConstraints& out) const noexcept {
        if (!std::isfinite(minWidth) || !std::isfinite(minHeight) ||
            std::isnan(maxWidth) || std::isnan(maxHeight) ||
            maxWidth == -std::numeric_limits<float>::infinity() ||
            maxHeight == -std::numeric_limits<float>::infinity()) return false;
        const float w = minWidth < 0 ? 0 : minWidth;
        const float h = minHeight < 0 ? 0 : minHeight;
        out = {w, maxWidth < w ? w : maxWidth, h, maxHeight < h ? h : maxHeight};
        return true;
    }
    bool operator==(const BoxConstraints& b) const noexcept {
        return minWidth == b.minWidth && maxWidth == b.maxWidth &&
            minHeight == b.minHeight && maxHeight == b.maxHeight;
    }
    Size constrain(Size s) const noexcept {
        return {clamp(s.width, minWidth, maxWidth), clamp(s.height, minHeight, maxHeight)};
    }
private:
    static float clamp(float value, float low, float high) noexcept {
        // A broken custom layout must not introduce NaN/infinity into geometry.
        if (!std::isfinite(value)) return low;
        return value < low ? low : (value > high ? high : value);
    }
};

} // namespace NanoUI
