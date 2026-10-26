#pragma once
#include <NanoUI/rendering/paint_canvas.h>
#include <NanoUI/rendering/render_object.h>

namespace NanoUI {
#if defined(__cpp_noexcept_function_type)
using PixelWrite = void (*)(int, int, std::uint32_t, void*) noexcept;
#else
using PixelWrite = void (*)(int, int, std::uint32_t, void*);
#endif
// Borrowed device, not a buffer owner. The callback must not throw or reenter.
struct PixelSink {
    PixelWrite write;
    void* context;
    PixelSink(PixelWrite callback = nullptr, void* value = nullptr) noexcept
        : write(callback), context(value) {}
};
// Store one persistent adapter per UI surface; its fixed depth64 stack is ~1.5KB.
// No allocation, clear, display, initialization, or device timing occurs here.
class PixelCanvas : public PaintCanvas {
public:
    PixelCanvas(int width, int height, PixelSink sink,
                Rect clip = Rect(0, 0, 32767, 32767)) noexcept
        : width_(width), height_(height), sink_(sink) {
        error_ = width <= 0 || height <= 0 || width > 32767 || height > 32767 || !sink.write;
        if (!error_) beginFrame(clip);
    }
    bool error() const noexcept { return error_; }
    bool balanced() const noexcept { return depth_ == 0; }
    // Refresh caller clip only at a balanced frame boundary. Does not erase pixels.
    bool beginFrame(Rect clip) noexcept {
        if (error_) return false;
        if (depth_ || !valid(clip)) { error_ = true; return false; }
        state_.offset = {};
        state_.clip = clip.intersect(Rect(0, 0, static_cast<float>(width_), static_cast<float>(height_)));
        return true;
    }
    void save() noexcept override {
        if (error_) return;
        if (depth_ == RenderObject::maxDepth) { error_ = true; return; }
        stack_[depth_++] = state_;
    }
    void restore() noexcept override {
        if (error_) return;
        if (!depth_) { error_ = true; return; }
        state_ = stack_[--depth_];
    }
    void translate(Offset offset) noexcept override {
        if (error_) return;
        const auto next = state_.offset + offset;
        if (!offset.isFinite() || !next.isFinite()) { error_ = true; return; }
        state_.offset = next;
    }
    void clipRect(Rect rect) noexcept override {
        if (error_) return;
        Rect mapped;
        if (!map(rect, mapped)) { error_ = true; return; }
        state_.clip = state_.clip.intersect(mapped);
    }
    void fillRect(Rect rect, std::uint32_t color) noexcept override {
        if (error_) return;
        Rect mapped;
        if (!map(rect, mapped)) { error_ = true; return; }
        const auto clipped = mapped.intersect(state_.clip);
        if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) return;
        // Clamp in floating point before integer conversion. A pixel's center
        // must be inside the translated half-open rectangle, as on RasterCanvas.
        const int left = edge(clipped.left, width_), right = edge(clipped.right, width_);
        const int top = edge(clipped.top, height_), bottom = edge(clipped.bottom, height_);
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) sink_.write(x, y, color, sink_.context);
    }
private:
    struct State { Offset offset; Rect clip; };
    static int edge(float value, int limit) noexcept {
        if (value <= 0.5f) return 0;
        if (value >= static_cast<float>(limit) + 0.5f) return limit;
        return static_cast<int>(std::ceil(value - 0.5f));
    }
    static bool valid(Rect r) noexcept {
        return std::isfinite(r.left) && std::isfinite(r.top) &&
               std::isfinite(r.right) && std::isfinite(r.bottom);
    }
    bool map(Rect r, Rect& result) const noexcept {
        if (!valid(r)) return false;
        result = Rect(r.left + state_.offset.dx, r.top + state_.offset.dy,
                      r.right + state_.offset.dx, r.bottom + state_.offset.dy);
        return valid(result);
    }
    int width_, height_;
    PixelSink sink_;
    State state_;
    State stack_[RenderObject::maxDepth];
    std::size_t depth_ = 0;
    bool error_ = false;
};
} // namespace NanoUI
