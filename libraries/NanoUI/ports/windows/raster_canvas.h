#pragma once

#include <NanoUI/rendering/paint_canvas.h>
#include <NanoUI/rendering/render_object.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

namespace NanoUI {

// Host-only software canvas. Colors use 0xRRGGBBAA; pixels are row-major.
class RasterCanvas final : public PaintCanvas {
public:
    RasterCanvas(std::size_t width, std::size_t height)
        : width_(width), height_(height), pixels_() {
        std::size_t count = 0;
        if (width == 0 || height == 0 || !safePixelCount(width, height, count) ||
            count > pixels_.max_size()) {
            error_ = true;
        } else {
            pixels_.assign(count, 0);
        }
        state_.clip = Rect(0, 0, static_cast<float>(width), static_cast<float>(height));
    }

    void clear(std::uint32_t color) noexcept { std::fill(pixels_.begin(), pixels_.end(), color); }
    std::uint32_t pixel(std::size_t x, std::size_t y) const noexcept {
        return !error_ && !pixels_.empty() && x < width_ && y < height_
            ? pixels_[y * width_ + x] : 0;
    }
    bool balanced() const noexcept { return depth_ == 0; }
    bool error() const noexcept { return error_; }

    void save() noexcept override {
        if (error_) return;
        if (depth_ == stack_.size()) { error_ = true; return; }
        stack_[depth_++] = state_;
    }
    void restore() noexcept override {
        if (error_) return;
        if (depth_ == 0) { error_ = true; return; }
        state_ = stack_[--depth_];
    }
    void translate(Offset offset) noexcept override {
        if (error_) return;
        if (!offset.isFinite()) { error_ = true; return; }
        const float x = state_.offset.dx + offset.dx, y = state_.offset.dy + offset.dy;
        if (!finite(x) || !finite(y)) { error_ = true; return; }
        state_.offset = Offset(x, y);
    }
    void clipRect(Rect rect) noexcept override {
        if (error_) return;
        Rect translated;
        if (!mapRect(rect, translated)) { error_ = true; return; }
        state_.clip = state_.clip.intersect(translated);
    }
    void fillRect(Rect rect, std::uint32_t color) noexcept override {
        if (error_) return;
        Rect mapped;
        if (!mapRect(rect, mapped)) { error_ = true; return; }
        const Rect r = mapped.intersect(state_.clip);
        if (r.right <= r.left || r.bottom <= r.top || width_ == 0 || height_ == 0) return;

        // A pixel is covered iff its center lies in the half-open rectangle.
        const double left = r.left, right = r.right, top = r.top, bottom = r.bottom;
        const double maxX = static_cast<double>(width_ - 1), maxY = static_cast<double>(height_ - 1);
        const double x0d = left <= 0.5 ? 0 : std::ceil(left - 0.5);
        const double y0d = top <= 0.5 ? 0 : std::ceil(top - 0.5);
        const double x1d = right >= maxX + 0.5 ? maxX : right - 0.5;
        const double y1d = bottom >= maxY + 0.5 ? maxY : bottom - 0.5;
        if (x1d < x0d || y1d < y0d) return;
        const std::size_t x0 = static_cast<std::size_t>(x0d < 0 ? 0 : x0d);
        const std::size_t y0 = static_cast<std::size_t>(y0d < 0 ? 0 : y0d);
        const std::size_t x1 = static_cast<std::size_t>(x1d);
        const std::size_t y1 = static_cast<std::size_t>(y1d);
        for (std::size_t y = y0; y <= y1; ++y)
            for (std::size_t x = x0; x <= x1; ++x)
                if (static_cast<double>(x) + 0.5 >= left && static_cast<double>(x) + 0.5 < right &&
                    static_cast<double>(y) + 0.5 >= top && static_cast<double>(y) + 0.5 < bottom)
                    pixels_[y * width_ + x] = color;
    }

    bool writePpm(const char* path) const {
        if (!path || !*path || error_) return false;
        std::ofstream out(path, std::ios::binary);
        if (!out) return false;
        out << "P6\n" << width_ << ' ' << height_ << "\n255\n";
        for (std::uint32_t c : pixels_) {
            const char rgb[] = {static_cast<char>((c >> 24) & 0xff),
                                static_cast<char>((c >> 16) & 0xff),
                                static_cast<char>((c >> 8) & 0xff)};
            out.write(rgb, sizeof rgb);
        }
        out.close();
        return out.good();
    }

private:
    struct State { Offset offset; Rect clip; };
    static constexpr std::size_t capacity_ = RenderObject::maxDepth;
    static bool safePixelCount(std::size_t w, std::size_t h, std::size_t& count) noexcept {
        if (w == 0 || h == 0 || w > std::numeric_limits<std::size_t>::max() / h) return false;
        count = w * h;
        return true;
    }
    static bool finite(float v) noexcept { return v == v && v <= std::numeric_limits<float>::max() && v >= -std::numeric_limits<float>::max(); }
    bool mapRect(Rect r, Rect& out) const noexcept {
        if (!finite(r.left) || !finite(r.right) || !finite(r.top) || !finite(r.bottom)) return false;
        const float l = r.left + state_.offset.dx, rr = r.right + state_.offset.dx;
        const float t = r.top + state_.offset.dy, b = r.bottom + state_.offset.dy;
        if (!finite(l) || !finite(rr) || !finite(t) || !finite(b)) return false;
        out = Rect(l, t, rr, b);
        return true;
    }

    std::size_t width_, height_;
    std::vector<std::uint32_t> pixels_;
    State state_;
    std::array<State, capacity_> stack_{};
    std::size_t depth_ = 0;
    bool error_ = false;
};

} // namespace NanoUI
