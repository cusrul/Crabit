#ifndef CORE_GEOMETRY_H
#define CORE_GEOMETRY_H

#include <stdint.h>

// 自己实现简单的 min/max，避开 SDK 的宏冲突
template <typename T>
static T NanoMin(T a, T b) { return (a < b) ? a : b; }

template <typename T>
static T NanoMax(T a, T b) { return (a > b) ? a : b; }

struct Point {
    int16_t x, y;
};

struct Size {
    int16_t width, height;
};

// Widen arithmetic before narrowing back to stored pixel dimensions.
inline int16_t NanoExtent(int32_t value) {
    return static_cast<int16_t>(NanoMax<int32_t>(0, NanoMin<int32_t>(value, 32767)));
}

inline int16_t NanoCoordinate(int32_t value) {
    return static_cast<int16_t>(NanoMax<int32_t>(-32768, NanoMin<int32_t>(value, 32767)));
}

struct Insets {
    int16_t left, top, right, bottom;
    static Insets all(int16_t value) { return {value, value, value, value}; }
    int32_t horizontal() const { return static_cast<int32_t>(left) + right; }
    int32_t vertical() const { return static_cast<int32_t>(top) + bottom; }
};

struct Rect {
    int16_t left, top, right, bottom;

    int16_t width() const { return right - left; }
    int16_t height() const { return bottom - top; }

    void unite(const Rect& other) {
        if (other.width() <= 0) return;
        left = NanoMin(left, other.left);
        top = NanoMin(top, other.top);
        right = NanoMax(right, other.right);
        bottom = NanoMax(bottom, other.bottom);
    }

    bool intersects(const Rect& other) const {
        if (left >= right || top >= bottom || other.left >= other.right || other.top >= other.bottom) return false;
        return (left < other.right && right > other.left &&
                top < other.bottom && bottom > other.top);
    }
    Rect intersection(Rect other) const {
        Rect r = {NanoMax(left, other.left), NanoMax(top, other.top),
                  NanoMin(right, other.right), NanoMin(bottom, other.bottom)};
        if (r.right < r.left) r.right = r.left;
        if (r.bottom < r.top) r.bottom = r.top;
        return r;
    }
};

struct BoxConstraints {
    enum { Unbounded = 32767 };
    int16_t minWidth, maxWidth;
    int16_t minHeight, maxHeight;

    // 1. 严格约束 (宽高固定) - 通过 Size
    static BoxConstraints tight(Size size) {
        return {size.width, size.width, size.height, size.height};
    }

    // 2. 严格约束 (宽高固定) - 通过具体数值 【修复报错关键】
    static BoxConstraints tight(int16_t w, int16_t h) {
        return {w, w, h, h};
    }

    // 3. 宽松约束 (0 ~ max)
    static BoxConstraints loose(Size size) {
        return {0, size.width, 0, size.height};
    }

    // 4. 获取最大尺寸 【修复报错关键】
    Size maxSize() const {
        return {maxWidth, maxHeight};
    }

    // Operations require valid, nonnegative constraints and Insets.
    Size constrainSize(Size desired) const {
        return {NanoMax(minWidth, NanoMin(desired.width, maxWidth)),
                NanoMax(minHeight, NanoMin(desired.height, maxHeight))};
    }
    bool contains(Size value) const {
        return value.width >= minWidth && value.width <= maxWidth &&
               value.height >= minHeight && value.height <= maxHeight;
    }
    BoxConstraints loosen() const { return {0, maxWidth, 0, maxHeight}; }
    bool hasBoundedWidth() const { return maxWidth != Unbounded; }
    bool hasBoundedHeight() const { return maxHeight != Unbounded; }
    BoxConstraints deflate(Insets insets) const {
        return {NanoExtent(minWidth - insets.horizontal()),
                hasBoundedWidth() ? NanoExtent(maxWidth - insets.horizontal()) : static_cast<int16_t>(Unbounded),
                NanoExtent(minHeight - insets.vertical()),
                hasBoundedHeight() ? NanoExtent(maxHeight - insets.vertical()) : static_cast<int16_t>(Unbounded)};
    }
    // Parent bounds take precedence over explicit child size requests.
    BoxConstraints enforce(BoxConstraints parent) const {
        return {NanoMax(parent.minWidth, NanoMin(minWidth, parent.maxWidth)),
                NanoMax(parent.minWidth, NanoMin(maxWidth, parent.maxWidth)),
                NanoMax(parent.minHeight, NanoMin(minHeight, parent.maxHeight)),
                NanoMax(parent.minHeight, NanoMin(maxHeight, parent.maxHeight))};
    }
};

#endif
